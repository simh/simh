/* b5500_dtc.c: Burrioughs 5500 Data Communications 

   Copyright (c) 2016, Richard Cornwell

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

*/

#include "b5500_defs.h"
#include "sim_timer.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#if (NUM_DEVS_DTC > 0) 

#define UNIT_DTC        UNIT_ATTABLE | UNIT_DISABLE | UNIT_IDLE

#define DTC_MLINES      32      /* mux lines */
#define DTC_TLINES      8
#define DTC_BUFSIZ      112     /* max chan transfer */


#define DTCSTA_READ     000400  /* Read flag */
#define DTCSTA_BINARY   004000  /* Bypass translation */
#define DTCSTA_INHIBIT  040000  /* Interrogate or Read/Write */

/* Flags in WC Field */
#define DTCSTA_TTU      0740    /* TTU number */
#define DTCSTA_GM       0020    /* Ignore GM on transfer */
#define DTCSTA_BUF      0017    /* Buffer Number */

/* Interrogate 
        D28 - Busy         DEV_ERROR 
        D27 - Write Ready  DEV_EOF
        D24 - Read Ready   DEV_IORD  */
/* Abnormal flag = DEV_WCFLG */
/* Buffer full/GM flag = D25 */
/* Buffer wrong state = DEV_EOF D27 */
/* Buffer Busy = DEV_ERROR D28 */
/* DTC not ready or buffer, DEV_NOTRDY D30 */
/* in u3 is device address */
/* in u4 is current address */
/* in u5 Line number */
#define DTC_CHAN        000003  /* Channel number */
#define DTC_RD          000004  /* Executing a read command */
#define DTC_WR          000010  /* Executing a write command */
#define DTC_INQ         000020  /* Executing an interragte command */
#define DTC_RDY         000040  /* Device Ready */
#define DTC_BIN         000100  /* Transfer in Binary mode */
#define DTC_IGNGM       000200  /* Ignore GM on transfer */

#define BufNotReady     0       /* Device not connected */
#define BufIdle         1       /* Buffer in Idle state */
#define BufInputBusy    2       /* Buffer being filled */
#define BufReadRdy      3       /* Buffer ready to read */
#define BufWrite        4       /* Buffer writing */
#define BufWriteRdy     5       /* Buffer ready for write */
#define BufOutBusy      6       /* Buffer outputing */
#define BufRead         7       /* Buffer reading */
#define BufSMASK        7       /* Buffer state mask */
#define BufAbnormal     010     /* Abnornmal flag */
#define BufGM           020     /* Buffer term with GM */
#define BufIRQ          040     /* Buffer ready */

/* Not connected line:
        BufNotReady.

        Write:
            BufNotReady -> 0x34 (EOF,ERROR,NR)
            BufIdle -> BufWrite (set GM if set.)
            BufReadReady ->  0x20 (EOF).
            BufInputBusy, BufWrite -> 0x30
                -> 0x34 (EOF,ERROR) 
            BufWriteRdy -> BufWrite.

        Write Done:
             BufOutBusy. 

        Read:
            BufNotReady -> 0x34 (EOF,ERROR,NR)
            BufIdle -> 0x34 (EOF,ERROR,NR)
            BufInputBusy, BufOutBusy -> 0x30 (EOF,ERROR)
            BufReadRdy -> return buffer. -> BufIdle

        Interogate:
            return BufWriteRdy/BufWriteFull
            return BufReadRdy.

        Recieve Char:

        Connect:
            State BufWriteRdy.

        Output Done:
            BufGM -> BufIdle
                  -> BufWriteRdy
*/

/* Translate chars
       
        output:
      ! -> LF.
      < -> RO.
      > -> X-OFF
      } -> Disconnect line
      ~ -> End of message.

        input:
      ~/_/CR -> End of message. 
                BufReadRdy, IRQ. 
      </BS -> Back up one char.
      !/   -> Disconnect insert }
                BufReadRdy, IRQ. 
      ^B    -> Clear input. 
                BufIdle
      ^E   -> set abnormal, buffer to BufWriteRdy.
      ^L   -> Clear input.
                BufIdle
      ?    -> Set abnormal
      Char: Buf to BufInputBsy. Insert char.
            if Fullbuff, BufReadRdy, IRQ, 
 
*/
 
        

t_stat              dtc_srv(UNIT *);
t_stat              dtco_srv(UNIT *);
t_stat              dtc_attach(UNIT *, CONST char *);
t_stat              dtc_detach(UNIT *);
t_stat              dtc_reset(DEVICE *);
t_stat              dtc_setnl (UNIT *, int32, CONST char *, void *);
t_stat              dtc_set_log (UNIT *, int32, CONST char *, void *);
t_stat              dtc_set_nolog (UNIT *, int32, CONST char *, void *);
t_stat              dtc_show_log (FILE *, UNIT *, int32, CONST void *);
t_stat              dtc_set_buf (UNIT *, int32, CONST char *, void *);
t_stat              dtc_show_buf (FILE *, UNIT *, int32, CONST void *);
t_stat              dtc_help(FILE *, DEVICE *, UNIT *, int32, const char *);
t_stat              dtc_help_attach (FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *dtc_description(DEVICE *);


int32               tmxr_poll;

uint8               dtc_buf[DTC_MLINES][DTC_BUFSIZ];
TMLN                dtc_ldsc[DTC_MLINES];                       /* line descriptors */
TMXR                dtc_desc = { DTC_TLINES, 0, 0, dtc_ldsc };  /* mux descriptor */
uint8               dtc_lstatus[DTC_MLINES];                    /* Line status */
uint16              dtc_bufptr[DTC_MLINES];                     /* Buffer pointer */
uint16              dtc_bsize[DTC_MLINES];                      /* Buffer size */
uint16              dtc_blimit[DTC_MLINES];                     /* Buffer size */
int                 dtc_bufsize = DTC_BUFSIZ;


MTAB                dtc_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &dtc_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &dtc_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dtc_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dtc_desc, "Display multiplexer statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dtc_setnl, &tmxr_show_lines, (void *) &dtc_desc, "Display number of lines" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "BUFSIZE", "BUFSIZE=n",
        &dtc_set_buf, &dtc_show_buf, (void *)&dtc_bufsize, "Set buffer size" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &dtc_set_log, NULL, (void *)&dtc_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG",
        &dtc_set_nolog, NULL, (void *)&dtc_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &dtc_show_log, (void *)&dtc_desc, "Display logging for all lines" },
    {0}
};


UNIT                dtc_unit[] = {
    {UDATA(&dtc_srv, UNIT_DTC, 0)},                   /* DTC */
    {UDATA(&dtco_srv, UNIT_DIS, 0)},                  /* DTC server process */
};

DEVICE              dtc_dev = {
    "DTC", dtc_unit, NULL, dtc_mod,
    2, 8, 15, 1, 8, 64,
    NULL, NULL, &dtc_reset, NULL, &dtc_attach, &dtc_detach,
    NULL, DEV_DISABLE | DEV_DEBUG | DEV_MUX, 0, dev_debug,
    NULL, NULL, &dtc_help, &dtc_help_attach, (void *)&dtc_desc,
    &dtc_description
};




/* Start off a terminal controller command */
t_stat dtc_cmd(uint16 cmd, uint16 dev, uint8 chan, uint16 *wc)
{
    UNIT        *uptr;
    int         ttu;
    int         buf;

    uptr = &dtc_unit[0];

    /* If unit disabled return error */
    if (uptr->flags & UNIT_DIS) 
        return SCPE_NODEV;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;

    /* Check if drive is ready to recieve a command */
    if ((uptr->u5 & DTC_RDY) == 0) 
        return SCPE_BUSY;

    uptr->u5 = chan;
    ttu = (*wc & DTCSTA_TTU) >> 5;
    buf = (*wc & DTCSTA_BUF); 
    /* Set the Terminal unit. */
    if (ttu == 0) 
        uptr->u4 = -1;
    else {
        uptr->u4 = buf + ((ttu-1) * 15);
    }
    if (*wc & DTCSTA_GM)
        uptr->u5 |= DTC_IGNGM;
    if (cmd & DTCSTA_READ) 
        uptr->u5 |= DTC_RD;
    else if (cmd & DTCSTA_INHIBIT)
        uptr->u5 |= DTC_INQ;
    else
        uptr->u5 |= DTC_WR;

    if (cmd & DTCSTA_BINARY)
        uptr->u5 |= DTC_BIN;

    sim_debug(DEBUG_CMD, &dtc_dev, "Datacomm access %s %06o %d %04o\n",
                (uptr->u5 & DTC_RD) ? "read" : ((uptr->u5 & DTC_INQ) ? "inq" :
                  ((uptr->u5 & DTC_WR) ? "write" : "unknown")), 
                 uptr->u5, uptr->u4, *wc);
    sim_activate(uptr, 5000);
    return SCPE_OK;
}
        

/* Handle processing terminal controller commands */
t_stat dtc_srv(UNIT * uptr)
{
    int                 chan = uptr->u5 & DTC_CHAN;
    uint8               ch;
    int                 ttu;
    int                 buf;
    int                 i;
    int                 line = uptr->u4;
    

    
    /* Process interrage command */
    if (uptr->u5 & DTC_INQ) {
        if (line == -1) {
            buf = -1;
            for(i = 0; i < DTC_MLINES; i++) {
                if (dtc_lstatus[i]& BufIRQ) {
                   if ((dtc_lstatus[i] & BufSMASK) == BufReadRdy) 
                      buf = i;
                   if ((dtc_lstatus[i] & BufSMASK) == BufWriteRdy ||
                       (dtc_lstatus[i] & BufSMASK) == BufIdle) {
                      line = i;
                      break;
                   }
                }
            }
            sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm inqury found %d %d ",
                line, buf);
            if (line != -1) {
                if ((dtc_lstatus[line] & BufSMASK) == BufWriteRdy) {
                     chan_set_eof(chan);
                     sim_debug(DEBUG_DETAIL, &dtc_dev, " writerdy ");
                } else {
                     sim_debug(DEBUG_DETAIL, &dtc_dev, " idle ");
                }
            } else if (buf != -1) {
                chan_set_read(chan);
                sim_debug(DEBUG_DETAIL, &dtc_dev, " readrdy ");
                line = buf;
            }

            if (line != -1) {
                if (dtc_lstatus[line] & BufAbnormal) {
                    chan_set_wcflg(chan);
                    sim_debug(DEBUG_DETAIL, &dtc_dev, " abnormal ");
                }
                dtc_lstatus[line] &= ~BufIRQ;
            }
            sim_debug(DEBUG_DETAIL, &dtc_dev, " %03o ", dtc_lstatus[i]);
        } else {
            if (line > dtc_desc.lines) {
                chan_set_notrdy(chan);
            } else {
                switch(dtc_lstatus[line] & BufSMASK) {
                case BufReadRdy:
                      chan_set_read(chan);
                      sim_debug(DEBUG_DETAIL, &dtc_dev, " readrdy ");
                      break;
                case BufWriteRdy:
                      chan_set_eof(chan);
                      sim_debug(DEBUG_DETAIL, &dtc_dev, " writerdy ");
                      break;
                case BufIdle:
                      sim_debug(DEBUG_DETAIL, &dtc_dev, " idle ");
                      break;
                default:
                      chan_set_error(chan);
                      sim_debug(DEBUG_DETAIL, &dtc_dev, " busy ");
                      break;
                }
            }
            if (dtc_lstatus[line] & BufAbnormal) {
                chan_set_wcflg(chan);
                sim_debug(DEBUG_DETAIL, &dtc_dev, " abnormal ");
            }
            dtc_lstatus[line] &= ~BufIRQ;
            chan_set_wc(uptr->u4, 0);
            chan_set_end(chan);
            sim_debug(DEBUG_DETAIL, &dtc_dev, " %03o ", dtc_lstatus[line]);
        }
        if (line != -1) {
            for (ttu = 1; line > 15; ttu++)
                line -= 15;
        } else {
            ttu = line = 0;
        }
        chan_set_wc(chan, (ttu << 5) | line);
        chan_set_end(chan);
        uptr->u5 = DTC_RDY;
        sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm inqury %d %d\n",
                ttu, line);
    }
    /* Process for each unit */
    if (uptr->u5 & DTC_WR) {
        if (line > dtc_desc.lines || line == -1) {
            sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm write invalid %d\n",
                         line);
            chan_set_notrdy(chan);
            chan_set_end(chan);
            uptr->u5 = DTC_RDY;
            return SCPE_OK;
        } 
        /* Validate that we can send data to buffer */
        i = dtc_lstatus[line] & BufSMASK;
        switch(i) {
        case BufNotReady:
                chan_set_notrdy(chan);
        case BufInputBusy:
        case BufRead:
        case BufReadRdy:
                chan_set_error(chan);
        case BufOutBusy:
                chan_set_eof(chan);
                chan_set_end(chan);
                uptr->u5 = DTC_RDY;
                sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm write busy %d %d\n", 
                        line, i);
                return SCPE_OK;

        /* Ok to start filling */
        case BufIdle:
        case BufWriteRdy:
                dtc_bufptr[line] = 0;
                dtc_bsize[line] = 0;
                sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm write start %d\n",
                         line);
                break;

        /* Continue filling */
        case BufWrite:
                break;
        }
        
        if (chan_read_char(chan, &ch, dtc_bufptr[line] >= dtc_blimit[line])) {
                sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm write done %d %d ",
                         line, dtc_bufptr[line]);
                dtc_bsize[line] = dtc_bufptr[line];
                dtc_bufptr[line] = 0;
                if (dtc_lstatus[line] & BufAbnormal) {
                    chan_set_wcflg(chan);
                }
                /* Empty write, clears flags */
                if (dtc_bsize[line] == 0) {
                    sim_debug(DEBUG_DETAIL, &dtc_dev, "empty\n");
                    if ((dtc_lstatus[line] & BufSMASK) != BufIdle) {
                        dtc_lstatus[line] = BufIRQ|BufIdle;
                        IAR |= IRQ_12;
                    }
                /* Check if we filled up buffer */
                } else if (dtc_bsize[line] >= dtc_blimit[line]) {
                     dtc_lstatus[line] = BufOutBusy;
                     chan_set_gm(chan);
                     sim_debug(DEBUG_DETAIL, &dtc_dev, "full ");
                } else {
                     dtc_lstatus[line] = BufOutBusy|BufGM;
                     sim_debug(DEBUG_DETAIL, &dtc_dev, "gm ");
                }
                sim_debug(DEBUG_DETAIL, &dtc_dev, "\n");
                for (ttu = 1; line > 15; ttu++)
                    line -= 15;
                chan_set_wc(chan, (ttu << 5) | line);
                chan_set_end(chan);
                uptr->u5 = DTC_RDY;
                return SCPE_OK;
        } else {
              dtc_lstatus[line] = BufWrite;
              dtc_buf[line][dtc_bufptr[line]++] = ch & 077;
              sim_debug(DEBUG_DATA, &dtc_dev, "Datacomm write data %d %02o %d\n",
                          line, ch&077, dtc_bufptr[line]);
        }
        sim_activate(uptr, 5000);
    }

    if (uptr->u5 & DTC_RD) {
        if (line > dtc_desc.lines || line == -1) {
            sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm read nothing %d\n",
                 line);
            chan_set_notrdy(chan);
            chan_set_end(chan);
            uptr->u5 = DTC_RDY;
            return SCPE_OK;
        } 
        /* Validate that we can send data to buffer */
        i = dtc_lstatus[line] & BufSMASK;
        switch(i) {
        case BufNotReady:
                chan_set_notrdy(chan);
        case BufInputBusy:
                chan_set_error(chan);
        case BufWriteRdy:
        case BufOutBusy:
        case BufIdle:
        case BufWrite:
                chan_set_eof(chan);
                chan_set_end(chan);
                uptr->u5 = DTC_RDY;
                sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm read busy %d %d\n",
                         line, i);
                return SCPE_OK;

        /* Ok to start filling */
        case BufReadRdy:
                dtc_lstatus[line] = (dtc_lstatus[line] & 030) | BufRead;
                dtc_bufptr[line] = 0;
                sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm read starting %d\n",
                         line);
                break;

        /* Continue filling */
        case BufRead:
                break;
        }

        ch = dtc_buf[line][dtc_bufptr[line]++];
        /* If no buffer, error out */
        if (chan_write_char(chan, &ch, dtc_bufptr[line] >= dtc_bsize[line])) {
                /* Check if we filled up buffer */
                if (dtc_lstatus[line] & BufGM) {
                     chan_set_gm(chan);
                     sim_debug(DEBUG_DETAIL, &dtc_dev, "gm ");
                }
                if (dtc_lstatus[line] & BufAbnormal) 
                     chan_set_wcflg(chan);
                if (dtc_ldsc[line].conn == 0)   /* connected? */
                    dtc_lstatus[line] = BufIRQ|BufNotReady;
                else
                    dtc_lstatus[line] = BufIRQ|BufIdle;
                dtc_bsize[line] = 0;
                sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm read done %d\n",
                         line);
                for (ttu = 1; line > 15; ttu++)
                    line -= 15;
                chan_set_wc(chan, (ttu << 5) | line);
                chan_set_end(chan);
                uptr->u5 = DTC_RDY;
                IAR |= IRQ_12;
                return SCPE_OK;
        } else {
             sim_debug(DEBUG_DATA, &dtc_dev, "Datacomm read data %d %02o %d\n",
                          line, ch & 077, dtc_bufptr[line]);
        }
        sim_activate(uptr, 5000);
    }
    return SCPE_OK;
}

/* Unit service - receive side

   Poll all active lines for input
   Poll for new connections */

t_stat
dtco_srv(UNIT * uptr)
{
    int                 c, ln, t, c1;

    sim_clock_coschedule(uptr, tmxr_poll);
    ln = tmxr_poll_conn(&dtc_desc);     /* look for connect */
    if (ln >= 0) {              /* got one? */
        dtc_blimit[ln] = dtc_bufsize-1;
        dtc_lstatus[ln] = BufIRQ|BufAbnormal|BufWriteRdy;
        IAR |= IRQ_12;
        sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm connect %d\n", ln);
    } 

    /* For each line that is in idle state enable recieve */
    for (ln = 0; ln < dtc_desc.lines; ln++) {
        if (dtc_ldsc[ln].conn &&
                (dtc_lstatus[ln] & BufSMASK) == BufIdle) {
           dtc_ldsc[ln].rcve = 1;
        }
    }
    tmxr_poll_rx(&dtc_desc);    /* poll for input */
    for (ln = 0; ln < DTC_MLINES; ln++) {       /* loop thru mux */
        /* Check for disconnect */
        if (dtc_ldsc[ln].conn == 0) {   /* connected? */
             switch(dtc_lstatus[ln] & BufSMASK) {
             case BufIdle:              /* Idle, throw in EOT */
             case BufWriteRdy:          /* Awaiting output, terminate */
                  dtc_bufptr[ln] = 0;
             case BufInputBusy:         /* reading, terminate with EOT */
                  dtc_buf[ln][dtc_bufptr[ln]++] = 017;
                  dtc_bsize[ln] = dtc_bufptr[ln];
                  dtc_lstatus[ln] = BufIRQ|BufAbnormal|BufReadRdy;
                  IAR |= IRQ_12;
                  break;
             case BufOutBusy:           /* Terminate Output */
                  dtc_lstatus[ln] = BufIRQ|BufIdle;
                  dtc_bsize[ln] = 0;
                  IAR |= IRQ_12;
                  break;
             default:                   /* Other cases, ignore until 
                                           in better state */
                  break;
             break;
             }
             continue;                  /* Skip if not connected */
        }
        switch(dtc_lstatus[ln] & BufSMASK) {
        case BufIdle:
             /* If we have any data to receive */
             if (tmxr_rqln(&dtc_ldsc[ln]) > 0) 
                dtc_lstatus[ln] = BufInputBusy;
             else 
                break;          /* Nothing to do */
             sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm recieve %d idle\n",
                         ln);
             dtc_bufptr[ln] = 0;
             dtc_bsize[ln] = 0;
        case BufInputBusy:
             t = 1;
             while (t && tmxr_rqln(&dtc_ldsc[ln]) != 0) {
                 c = tmxr_getc_ln(&dtc_ldsc[ln]) & 0x7f;   /* get char */
                 c1 = ascii_to_con[c];
                 switch(c) {
                 case '\005':   /* ^E ENQ who-are-you */
                       dtc_lstatus[ln] &= ~(BufSMASK);
                       dtc_lstatus[ln] |= BufIRQ|BufAbnormal|BufWriteRdy;
                       IAR |= IRQ_12;
                       sim_debug(DEBUG_DETAIL, &dtc_dev, 
                                        "Datacomm recieve ENQ %d\n", ln);
                       t = 0;
                       break;
                 case '\003':   /* ^B send STX */
                       dtc_lstatus[ln] &= ~BufSMASK;
                       dtc_lstatus[ln] |= BufIRQ|BufReadRdy|BufAbnormal;
                       dtc_buf[ln][0] = 0;
                       dtc_buf[ln][1] = 017;
                       dtc_buf[ln][2] = 077;
                       dtc_bsize[ln] = 1;
                       IAR |= IRQ_12;
                       t = 0;
                       break;
                 case '}':
                       dtc_buf[ln][dtc_bufptr[ln]++] = 017;
                       dtc_lstatus[ln] |= BufAbnormal;
                       /* Fall through to next */

                 case '\r':
                 case '\n':
                 case '~':
                       dtc_lstatus[ln] &= ~BufSMASK;
                       dtc_lstatus[ln] |= BufIRQ|BufReadRdy;
                        /* Force at least one character for GM */
                       dtc_buf[ln][dtc_bufptr[ln]++] = 077;
                       dtc_bsize[ln] = dtc_bufptr[ln];
                       IAR |= IRQ_12;
                       t = 0;
                       c1 = 0;
                       sim_debug(DEBUG_DETAIL, &dtc_dev,
                                 "Datacomm recieve %d return\n", ln);
                       break;
                 case '\025':   /* Control U clear input buffer. */
                       dtc_bsize[ln] = 0;
                       c1 = 0;
                       break;
                 case '\b':
                 case 0x7f:
                       if (dtc_bufptr[ln] > 0) {
                          tmxr_putc_ln(&dtc_ldsc[ln], '\b');
                          tmxr_putc_ln(&dtc_ldsc[ln], ' ');
                          tmxr_putc_ln(&dtc_ldsc[ln], '\b');
                          dtc_bufptr[ln]--;
                       } else {
                          tmxr_putc_ln(&dtc_ldsc[ln], '\007');
                       }
                       c1 = 0;
                       sim_debug(DEBUG_DATA, &dtc_dev, 
                                "Datacomm recieve %d backspace %d\n", ln, dtc_bufptr[ln]);
                       break;
                 case '?':
                       sim_debug(DEBUG_DATA, &dtc_dev, 
                                "Datacomm recieve %d ?\n", ln);
                       dtc_lstatus[ln] |= BufAbnormal;
                       tmxr_putc_ln(&dtc_ldsc[ln], '?');
                       dtc_buf[ln][dtc_bufptr[ln]++] = c1;
                       break;
                 default:
                       sim_debug(DEBUG_DATA, &dtc_dev,
                         "Datacomm recieve %d %02x %c %02o %d\n", ln, c, c, c1,
                             dtc_bufptr[ln]);
                 }
                 if (t && c1) {
                   tmxr_putc_ln(&dtc_ldsc[ln], con_to_ascii[c1]);
                   dtc_buf[ln][dtc_bufptr[ln]++] = c1;
                 }
                 if (dtc_bufptr[ln] >= dtc_blimit[ln]) {
                       sim_debug(DEBUG_DETAIL, &dtc_dev,
                                 "Datacomm recieve %d  full\n", ln);
                       dtc_lstatus[ln] &= ~(BufSMASK);
                       dtc_lstatus[ln] |= BufGM|BufIRQ|BufReadRdy;
                       dtc_bsize[ln] = dtc_bufptr[ln];
                       IAR |= IRQ_12;
                       t = 0;
                       break;
                 }
             }
                
             break;
        case BufOutBusy:
                /* Get next char and send to output */
             t = 1;
             while(t && dtc_bufptr[ln] < dtc_bsize[ln] && dtc_ldsc[ln].xmte) {
                 c = dtc_buf[ln][dtc_bufptr[ln]++];
                 c1 = con_to_ascii[c];
                 switch(c) {
                 case 057:      /* { */
                    c1 = '\r';          /* CR */
                    break;
                 case 032:      /* ! */
                    c1 = '\n';          /* LF */
                    break;
                 case 076:      /* < */
                    c1 = 0;             /* X-ON */
                    break;
                 case 016:      /* > */
                    c1 = 0;             /* DEL */
                    break;
                 case 017:      /* } */
                    /* Disconnect line */
                    tmxr_reset_ln(&dtc_ldsc[ln]);
                    sim_debug(DEBUG_DETAIL, &dtc_dev,
                         "Datacomm disconnect %d\n", ln);
                    t = 0;
                    continue;   /* On to next line */
                 }
                 sim_debug(DEBUG_DATA, &dtc_dev, 
                        "Datacomm transmit %d %02o %c\n", ln, c&077, c1);
                 tmxr_putc_ln(&dtc_ldsc[ln], c1);
                 if (c1 == '\n') {
                     tmxr_putc_ln(&dtc_ldsc[ln], '\r');
                 }
             }
             if (dtc_bufptr[ln] >= dtc_bsize[ln]) {
                if (dtc_lstatus[ln] & BufGM) {
                   sim_debug(DEBUG_DETAIL, &dtc_dev,
                                 "Datacomm idle %d\n", ln);
                   dtc_lstatus[ln] = BufIRQ|BufIdle;
                } else {
                   sim_debug(DEBUG_DETAIL, &dtc_dev, "Datacomm writerdy %d\n",
                         ln);
                   dtc_lstatus[ln] = BufIRQ|BufWriteRdy;
                }
                IAR |= IRQ_12;
             }
             break;
        default:
                /* Other states are an ignore */
             break;
        }
    }                           /* end for */
    tmxr_poll_tx(&dtc_desc);    /* poll xmt */

    return SCPE_OK;
}


t_stat 
dtc_reset(DEVICE *dptr) {
   if (dtc_unit[0].flags & UNIT_ATT) {
       sim_activate(&dtc_unit[1], 100); /* quick poll */
       iostatus |= DTC_FLAG;
   } else {
       sim_cancel(&dtc_unit[1]);
       iostatus &= ~DTC_FLAG;
   }
   return SCPE_OK;
}

/* Attach master unit */
t_stat
dtc_attach(UNIT * uptr, CONST char *cptr)
{
    int                 i;
    t_stat              r;

    r = tmxr_attach(&dtc_desc, uptr, cptr);     /* attach */
    if (r != SCPE_OK)
        return r;               /* error */
    sim_activate(&dtc_unit[1], 100);    /* quick poll */
    for (i = 0; i < DTC_MLINES; i++) {
        dtc_lstatus[i] = BufNotReady;   /* Device not connected */
    }
    uptr->u5 = DTC_RDY;
    iostatus |= DTC_FLAG;
    return SCPE_OK;
}

/* Detach master unit */

t_stat
dtc_detach(UNIT * uptr)
{
    int                 i;
    t_stat              r;

    r = tmxr_detach(&dtc_desc, uptr);   /* detach */
    for (i = 0; i < dtc_desc.lines; i++)
        dtc_ldsc[i].rcve = 0;   /* disable rcv */
    sim_cancel(uptr);           /* stop poll */
    uptr->u5 = 0;
    iostatus &= ~DTC_FLAG;
    return r;
}

/* SET LINES processor */

t_stat dtc_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 newln, i, t;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, DTC_MLINES, &r);
    if ((r != SCPE_OK) || (newln == dtc_desc.lines))
        return r;
    if ((newln == 0) || (newln > DTC_MLINES))
        return SCPE_ARG;
    if (newln < dtc_desc.lines) {
        for (i = newln, t = 0; i < dtc_desc.lines; i++)
            t = t | dtc_ldsc[i].conn;
        if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
        for (i = newln; i < dtc_desc.lines; i++) {
            if (dtc_ldsc[i].conn) {
                tmxr_linemsg (&dtc_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data (&dtc_ldsc[i]);
                }
            tmxr_detach_ln (&dtc_ldsc[i]);               /* completely reset line */
        }
    }
    if (dtc_desc.lines < newln)
        memset (dtc_ldsc + dtc_desc.lines, 0, sizeof(*dtc_ldsc)*(newln-dtc_desc.lines));
    dtc_desc.lines = newln;
    return dtc_reset (&dtc_dev);                         /* setup lines and auto config */
}

/* SET LOG processor */

t_stat dtc_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    char gbuf[CBUFSIZE];
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    cptr = get_glyph (cptr, gbuf, '=');
    if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
        return SCPE_ARG;
    ln = (int32) get_uint (gbuf, 10, dtc_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= dtc_desc.lines))
        return SCPE_ARG;
    return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

t_stat dtc_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, dtc_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= dtc_desc.lines))
        return SCPE_ARG;
    return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat dtc_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32 i;

    for (i = 0; i < dtc_desc.lines; i++) {
        fprintf (st, "line %d: ", i);
        tmxr_show_log (st, NULL, i, desc);
        fprintf (st, "\n");
        }
    return SCPE_OK;
}

/* SET BUFFER processor */

t_stat dtc_set_buf (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 bufsiz;

    if (cptr == NULL)
        return SCPE_ARG;
    bufsiz = (int32) get_uint (cptr, 10, DTC_BUFSIZ, &r);
    if ((r != SCPE_OK) || (bufsiz >= DTC_BUFSIZ))
        return SCPE_ARG;
    if (bufsiz > 0 && (bufsiz % 28) == 0) {
        dtc_bufsize = bufsiz;
        return SCPE_OK;
    }
    return SCPE_ARG;
}

/* SHOW BUFFER processor */

t_stat dtc_show_buf (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (st, "bufsize=%d ", dtc_bufsize);
    return SCPE_OK;
}

/* Show summary processor */

t_stat
dtc_summ(FILE * st, UNIT * uptr, int32 val, CONST void *desc)
{
    uint32              i, t;

    t = 0;
    for (i = 0; i < DTC_MLINES; i++)
        t = t + (dtc_ldsc[i].conn != 0);
    if (t == 1)
        fprintf(st, "1 connection");
    else
        fprintf(st, "%d connections", t);
    return SCPE_OK;
}

/* SHOW CONN/STAT processor */

t_stat
dtc_show(FILE * st, UNIT * uptr, int32 val, CONST void *desc)
{
    int32               i, cc;

    for (cc = 0; (cc < DTC_MLINES) && dtc_ldsc[cc].conn; cc++) ;
    if (cc) {
        for (i = 0; i < DTC_MLINES; i++) {
            if (dtc_ldsc[i].conn) {
                if (val)
                    tmxr_fconns(st, &dtc_ldsc[i], i);
                else
                    tmxr_fstats(st, &dtc_ldsc[i], i);
            }
        }
    } else
        fprintf(st, "all disconnected\n");
    return SCPE_OK;
}

t_stat dtc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{

fprintf (st, "B249 Terminal Control Unit\n\n");
fprintf (st, "The B249 is a terminal multiplexor.  Up to %d lines are supported.\n", DTC_MLINES);
fprintf (st, "The default number of lines is %d.  The number of lines can\n", DTC_MLINES);
fprintf (st, "be changed with the command\n\n");
fprintf (st, "   sim> SET %s LINES=n            set line count to n\n\n", dptr->name);
fprintf (st, "The default buffer size for all lines can be set to a multiple of 28\n");
fprintf (st, "to a max of %d characters. Changes will take effect when ", DTC_BUFSIZ);
fprintf (st, "devices connect.\nThis number must match what MCP believes to be the ");
fprintf (st, "buffer size.\n\n");
fprintf (st, "   sim> SET %s BUFSIZE=n          set buffer size to n\n\n", dptr->name);
fprintf (st, "The B249 supports logging on a per-line basis.  The command\n\n");
fprintf (st, "   sim> SET %s LOG=n=filename\n\n", dptr->name);
fprintf (st, "enables logging for the specified line(n) to the indicated file.  The command\n\n");
fprintf (st, "   sim> SET %s NOLOG=line\n\n", dptr->name);
fprintf (st, "disables logging for the specified line and closes any open log file.  Finally,\n");
fprintf (st, "the command:\n\n");
fprintf (st, "   sim> SHOW %s LOG\n\n", dptr->name);
fprintf (st, "displays logging information for all %s lines.\n\n", dptr->name);
fprintf (st, "Once the B249 is attached and the simulator is running, the B249 will listen for\n");
fprintf (st, "connections on the specified port.  It assumes that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connection remains open until disconnected by the\n");
fprintf (st, "simulated program, the Telnet client, a SET %s DISCONNECT command, or a\n", dptr->name);
fprintf (st, "DETACH %s command.\n\n", dptr->name);
fprintf (st, "Other special %s commands:\n\n", dptr->name);
fprintf (st, "   sim> SHOW %s CONNECTIONS           show current connections\n", dptr->name);
fprintf (st, "   sim> SHOW %s STATISTICS            show statistics for active connections\n", dptr->name);
fprintf (st, "   sim> SET %s DISCONNECT=linenumber  disconnects the specified line.\n\n\n", dptr->name);
fprintf (st, "All open connections are lost when the simulator shuts down or the %s is\n", dptr->name);
fprintf (st, "detached.\n\n");
dtc_help_attach (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

t_stat dtc_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
tmxr_attach_help (st, dptr, uptr, flag, cptr);
fprintf (st, "The terminal lines perform input and output through Telnet sessions connected\n");
fprintf (st, "to a user-specified port.  The ATTACH command specifies the port to be used:\n\n");
fprintf (st, "   sim> ATTACH  %s {interface:}port      set up listening port\n\n", dptr->name);
fprintf (st, "where port is a decimal number between 1 and 65535 that is not being used for\n");
fprintf (st, "other TCP/IP activities.  All terminals are considered Dialup to the B249.\n");
return SCPE_OK;
}

const char *dtc_description (DEVICE *dptr)
{
    return "B249 Terminal Control Unit";
}

#endif
