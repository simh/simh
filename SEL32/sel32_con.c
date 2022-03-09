/* sel32_con.c: SEL 32 Class F IOP processor console.

   Copyright (c) 2018-2022, James C. Bevier
   Portions provided by Richard Cornwell, Geert Rolf and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This is the standard console interface.  It is subchannel of the IOP 7e00.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as ASCII characters.
*/

#include "sel32_defs.h"
#include "sim_tmxr.h"

#if NUM_DEVS_CON > 0

#define UNIT_CON   UNIT_IDLE | UNIT_DISABLE

#define CMD     u3
/* Held in u3 is the device command and status */
#define CON_INCH    0x00    /* Initialize channel command */
#define CON_INCH2   0xf0    /* Initialize channel command for processing */
#define CON_WR      0x01    /* Write console */
#define CON_RD      0x02    /* Read console */
#define CON_NOP     0x03    /* No op command */
#define CON_SNS     0x04    /* Sense command */
#define CON_ECHO    0x0a    /* Read with Echo */
#define CON_RDBWD   0x0c    /* Read backward */
#define CON_CON     0x1f    /* connect line */
#define CON_DIS     0x23    /* disconnect line */
#define CON_RWD     0x37    /* TOF and write line */

#define CON_MSK     0xff    /* Command mask */

/* Status held in u3 */
/* controller/unit address in upper 16 bits */
#define CON_ATAT    0x4000  /* working on @@A input */
#define CON_READ    0x2000  /* Read mode selected */
#define CON_OUTPUT  0x1000  /* Output ready for unit */
#define CON_EKO     0x0800  /* Echo input character */
#define CON_REQ     0x0400  /* Request key pressed */
#define CON_CR      0x0200  /* Output at beginning of line */
#define CON_INPUT   0x0100  /* Input ready for unit */

/* Input buffer pointer held in u4 */

#define SNS     u5
/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ  0x80000000  /* Command reject */
#define SNS_INTVENT 0x40000000  /* Unit intervention required */
/* sense byte 3 */
#define SNS_RDY     0x80        /* device ready */
#define SNS_ONLN    0x40        /* device online */
#define SNS_DSR     0x08        /* data set ready */
#define SNS_DCD     0x04        /* data carrier detect */

/* std devices. data structures
    con_dev     Console device descriptor
    con_unit    Console unit descriptor
    con_reg     Console register list
    con_mod     Console modifiers list
*/

struct _con_data
{
    uint8       incnt;                  /* char count */
    uint8       ibuff[145];             /* Input line buffer */
}
con_data[NUM_UNITS_CON];

uint32  atbuf=0;                        /* attention buffer */
uint32  outbusy = 0;                    /* output waiting on timeout */
uint32  inbusy = 0;                     /* input waiting on timeout */

/* forward definitions */
t_stat  con_preio(UNIT *uptr, uint16 chan);
t_stat  con_startcmd(UNIT*, uint16, uint8);
void    con_ini(UNIT*, t_bool);
t_stat  con_srvi(UNIT*);
t_stat  con_srvo(UNIT*);
t_stat  con_haltio(UNIT *);
t_stat  con_rschnlio(UNIT *uptr);       /* Reset Channel */
t_stat  con_poll(UNIT *);
t_stat  con_reset(DEVICE *);

/* channel program information */
CHANP           con_chp[NUM_UNITS_CON] = {0};

MTAB    con_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr, &show_dev_addr, NULL},
    {0}
};

UNIT            con_unit[] = {
    {UDATA(&con_srvi, UNIT_CON, 0), 0, UNIT_ADDR(0x7EFC)},   /* 0 Input */
    {UDATA(&con_srvo, UNIT_CON, 0), 0, UNIT_ADDR(0x7EFD)},   /* 1 Output */
};

DIB             con_dib = {
    con_preio,      /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    con_startcmd,   /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    con_haltio,     /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O */
    NULL,           /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O */
    NULL,           /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    con_rschnlio,   /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    NULL,           /* t_stat (*iocl_io)(CHANP *chp, int32 tic_ok)) */  /* Process IOCL */
    con_ini,        /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    con_unit,       /* UNIT* units */                           /* Pointer to units structure */
    con_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NULL,           /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    NUM_UNITS_CON,  /* uint8 numunits */                        /* number of units defined */
    0x03,           /* uint8 mask */                            /* 2 devices - device mask */
    0x7e00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE  con_dev = {
    "CON", con_unit, NULL, con_mod,
    NUM_UNITS_CON, 8, 15, 1, 8, 8,
//  NULL, NULL, &con_reset, NULL, &con_attach, &con_detach,
    NULL, NULL, &con_reset, NULL, NULL, NULL,
    &con_dib, DEV_DIS|DEV_DISABLE|DEV_DEBUG, 0, dev_debug
};

/*
 * Console print routines.
 */
/* initialize the console chan/unit */
void con_ini(UNIT *uptr, t_bool f) {
    int     unit = (uptr - con_unit);       /* unit 0 */

    uptr->u4 = 0;                           /* no input count */
    con_data[unit].incnt = 0;               /* no input data */
    uptr->CMD &= LMASK;                     /* leave only chsa */
    uptr->SNS = SNS_RDY|SNS_ONLN;           /* status is online & ready */
    sim_cancel(uptr);                       /* stop input poll */
    if (unit == 0) {
        sim_activate(uptr, 1000);           /* start input poll */
    }
}

/* start a console operation */
t_stat con_preio(UNIT *uptr, uint16 chan) {
    DEVICE         *dptr = get_dev(uptr);
    int            unit = (uptr - dptr->units);

    if ((uptr->CMD & CON_MSK) != 0) {       /* just return if busy */
        sim_debug(DEBUG_CMD, dptr, "con_preio unit=%02x BUSY\n", unit);
        return SNS_BSY;
    }

    sim_debug(DEBUG_CMD, dptr, "con_preio unit=%02x OK\n", unit);
    return SCPE_OK;                         /* good to go */
}

/* start an I/O operation */
t_stat con_startcmd(UNIT *uptr, uint16 chan, uint8 cmd) {
    DEVICE      *dptr = uptr->dptr;
    int     unit = (uptr - con_unit);       /* unit 0 is read, unit 1 is write */

    if ((uptr->CMD & CON_MSK) != 0) {       /* is unit busy */
        sim_debug(DEBUG_EXP, dptr,
            "con_startcmd unit %01x chan %02x cmd %02x BUSY cmd %02x uptr %p\n",
            unit, chan, cmd, uptr->CMD, uptr);
        return SNS_BSY;                     /* yes, return busy */
    }

    sim_debug(DEBUG_DETAIL, dptr,
        "con_startcmd unit %01x chan %02x cmd %02x enter\n", unit, chan, cmd);

    /* substitute CON_INCH2 for CON_INCH for pprocessing */
    if (cmd == CON_INCH)
        cmd = CON_INCH2;                    /* save INCH command as 0xf0 */

    /* process the commands */
    switch (cmd & 0xFF) {
    case CON_ECHO:      /* 0x0a */          /* Read command w/ECHO */
        uptr->CMD |= CON_EKO;               /* save echo status */
    case CON_RD:        /* 0x02 */          /* Read command */
        atbuf = 0;                          /* reset attention buffer */
        uptr->CMD |= CON_READ;              /* show read mode */
        /* fall through */
    case CON_INCH2:     /* 0xf0 */          /* INCH command */
    case CON_RWD:       /* 0x37 */          /* TOF and write line */
    case CON_WR:        /* 0x01 */          /* Write command */
    case CON_NOP:       /* 0x03 */          /* NOP has do nothing */
    case CON_RDBWD:     /* 0x0c */          /* Read Backward */
        uptr->SNS |= (SNS_RDY|SNS_ONLN);    /* status is online & ready */
    case CON_CON:       /* 0x1f */          /* Connect, return Data Set ready */
    case CON_DIS:       /* 0x23 */          /* Disconnect has do nothing */
    case CON_SNS:       /* 0x04 */          /* Sense */
        uptr->CMD &= ~CON_MSK;              /* remove old CMD */
        uptr->CMD |= (cmd & CON_MSK);       /* save command */
        if (unit == 0) {
            sim_cancel(uptr);               /* stop input poll */
            sim_activate(uptr, 300);        /* start us off */
//          sim_activate(uptr, 1000);       /* start us off */
        }
        else
        /* using value 500 or larger causes diag to fail on 32/27 */
//      sim_activate(uptr, 500);            /* start us off */
//      sim_activate(uptr, 200);            /* start us off */
        sim_activate(uptr, 30);             /* start us off */
        return SCPE_OK;                     /* no status change */
        break;

    default:                                /* invalid command */
        break;
    }
    /* invalid command */
    uptr->SNS |= SNS_CMDREJ;                /* command rejected */
    sim_debug(DEBUG_EXP, dptr,
        "con_startcmd %04x: Invalid command %02x Sense %02x\n",
        chan, cmd, uptr->SNS);
    return SNS_CHNEND|SNS_DEVEND|STATUS_PCHK;
}

/* Handle output transfers for console */
t_stat con_srvo(UNIT *uptr) {
    DEVICE      *dptr = uptr->dptr;
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - con_unit);   /* unit 0 is read, unit 1 is write */
    int         cmd = uptr->CMD & CON_MSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */
    int         len = chp->ccw_count;       /* INCH command count */
    uint32      mema = chp->ccw_addr;       /* get inch or buffer addr */
    uint32      tstart;
    uint8       ch;
    static uint32 dexp;
    static int    cnt = 0;

    sim_debug(DEBUG_CMD, dptr,
        "con_srvo enter CMD %08x chsa %04x cmd %02x iocla %06x cnt %04x\n",
        uptr->CMD, chsa, cmd, chp->chan_caw, chp->ccw_count);

    switch (cmd) {

    /* if input tried from output device, error */
    case CON_RD:        /* 0x02 */          /* Read command */
    case CON_ECHO:      /* 0x0a */          /* Read command w/ECHO */
    case CON_RDBWD:     /* 0x0c */          /* Read Backward */
        /* if input requested for output device, give error */
        uptr->SNS |= SNS_CMDREJ;            /* command rejected */
        uptr->CMD &= LMASK;                 /* nothing left, command complete */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvo Read to output device CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_UNITCHK); /* unit check */
        break;

    case CON_CON:       /* 0x1f */          /* Connect, return Data Set ready */
        uptr->SNS |= (SNS_DSR|SNS_DCD);     /* Data set ready, Data Carrier detected */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvo CON CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
        uptr->CMD &= ~CON_MSK;              /* remove old CMD */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case CON_DIS:       /* 0x23 */          /* Disconnect has do nothing */
        uptr->SNS &= ~(SNS_DSR|SNS_DCD);    /* Data set not ready */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvo DIS CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
        uptr->CMD &= ~CON_MSK;              /* remove old CMD */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case CON_INCH2:      /* 0xf0 */         /* INCH command */
        uptr->CMD &= LMASK;                 /* nothing left, command complete */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvo INCH unit %02x: CMD %08x cmd %02x incnt %02x u4 %02x\n",
            unit, uptr->CMD, cmd, con_data[unit].incnt, uptr->u4);

        /* now call set_inch() function to write and test inch buffer addresses */
        /* 1-256 wd buffer is provided for 128 status dbl words */
        tstart = set_inch(uptr, mema, 128); /* new address & 128 entries */
        if ((tstart == SCPE_MEM) || (tstart == SCPE_ARG)) { /* any error */
            /* we have error, bail out */
            uptr->SNS |= SNS_CMDREJ;
            sim_debug(DEBUG_CMD, dptr,
                "con_srvo INCH Error unit %02x: CMD %08x cmd %02x incnt %02x u4 %02x\n",
                unit, uptr->CMD, cmd, con_data[unit].incnt, uptr->u4);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        sim_debug(DEBUG_CMD, dptr,
            "con_srvo INCH CMD %08x chsa %04x len %02x inch %06x\n", uptr->CMD, chsa, len, mema);
        /* WARNING, if SNS_DEVEND is not set, diags fail by looping in CON diag */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case CON_NOP:       /* 0x03 */          /* NOP has do nothing */
        uptr->CMD &= ~CON_MSK;              /* remove old CMD */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvo NOP CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case CON_SNS:       /* 0x04 */          /* Sense */
        /* value 4 is Data Set Ready */
        /* value 5 is Data carrier detected n/u */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvo cmd %04x: Cmd Sense %02x\n", chsa, uptr->SNS);
        /* value 4 is Data Set Ready */
        /* value 5 is Data carrier detected n/u */
        ch = uptr->SNS & 0xff;              /* Sense byte 3 */
        if (chan_write_byte(chsa, &ch)) {   /* write byte to memory */
            /* write error */
            cmd = 0;                        /* no cmd now */
            sim_debug(DEBUG_CMD, dptr,
                "con_srvo write error unit %02x: CMD %08x read %02x u4 %02x ccw_count %02x\n",
                unit, uptr->CMD, ch, uptr->u4, chp->ccw_count);
            uptr->CMD &= LMASK;             /* nothing left, command complete */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
            break;
        }
        uptr->CMD &= LMASK;                 /* nothing left, command complete */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* good return */
        break;

    case CON_RWD:       /* 0x37 */          /* TOF and write line */
    case CON_WR:        /* 0x01 */          /* Write command */
#ifdef DO_OLDWAY
        /* see if write complete */
        if (uptr->CMD & CON_OUTPUT) {
            /* write is complete, post status */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo write CMD %08x chsa %04x cmd %02x complete\n",
                uptr->CMD, chsa, cmd);
            uptr->CMD &= ~CON_MSK;          /* remove old CMD */
            uptr->CMD &= ~CON_OUTPUT;       /* remove output command */
/*RTC*/     outbusy = 0;                    /* output done */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
            break;
        }
/*RTC*/ outbusy = 1;                        /* tell clock output waiting */
        if (chan_read_byte(chsa, &ch) == SCPE_OK) {  /* get byte from memory */
            /* Write to device */
            ch &= 0x7f;                     /* make 7 bit w/o parity */
            dexp = dexp<<8;                 /* move up last chars */
            dexp |= ch;                     /* insert new char */
#ifdef DO_DYNAMIC_DEBUG
//          if ((cnt == 3) && (dexp == 0x4458503e)) {   /* test for "DXP>" */
            if ((cnt == 13) && (dexp == 0x4E542E48)) {   /* test for "NT.H" */
                cpu_dev.dctrl |= (DEBUG_INST|DEBUG_TRAP|DEBUG_IRQ); /* start instruction trace */
                con_dev.dctrl |= DEBUG_XIO|DEBUG_CMD;
//              sim_debug(DEBUG_INST, &cpu_dev, "|con_srvo DXP> received|\n");
                sim_debug(DEBUG_INST, &cpu_dev, "con_srvo CV.INT.H received start debug\n");
            }
            if ((cnt == 13) && (dexp == 0x52502E48)) {   /* test for "RP.H" */
                /* turn of debug trace because we are already hung */
                sim_debug(DEBUG_INST, &cpu_dev, "con_srvo got CV.TRP.H stopping debug\n");
                cpu_dev.dctrl &= ~(DEBUG_INST|DEBUG_TRAP|DEBUG_IRQ); /* start instruction trace */
                con_dev.dctrl &= ~(DEBUG_XIO|DEBUG_CMD);
            }
#endif
            sim_putchar(ch);                /* output next char to device */
            sim_debug(DEBUG_CMD, dptr,
                "con_srvo write wait %03x CMD %08x chsa %04x cmd %02x byte %d = %02x\n",
                1000, uptr->CMD, chsa, cmd, cnt, ch);
            cnt++;                          /* count chars output */
//01132022  sim_activate(uptr, 500);        /* wait for a while before next write */
            sim_activate(uptr, 50);         /* wait for a while before next write */
            break;
        }
        /* nothing left, finish up */
        cnt = 0;                            /* zero for next output */
        uptr->CMD |= CON_OUTPUT;            /* output command complete */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_srvo write wait %03x CMD %08x chsa %04x cmd %02x to complete\n",
            1000, uptr->CMD, chsa, cmd);
        sim_activate(uptr, 500);            /* wait for a while */
        break;
#else
        cnt = 0;                            /* zero count */
/*RTC*/ outbusy = 1;                        /* tell clock output waiting */
        mema = chp->ccw_addr;               /* get buffer addr */
        /* Write to device */
        while (chan_read_byte(chsa, &ch) == SCPE_OK) {  /* get byte from memory */
            /* HACK HACK HACK */
            ch &= 0x7f;                     /* make 7 bit w/o parity */
            dexp = dexp<<8;                 /* move up last chars */
            dexp |= ch;                     /* insert new char */
#ifdef DO_DYNAMIC_DEBUG
//          if ((cnt == 3) && (dexp == 0x4458503e)) {       /* test for "DXP>" */
            if ((cnt == 3) && (dexp == 0x44454641)) {       /* test for "DEFA" */
//              cpu_dev.dctrl |= (DEBUG_INST|DEBUG_IRQ);    /* start instruction trace */
                cpu_dev.dctrl |= (DEBUG_INST|DEBUG_TRAP|DEBUG_IRQ); /* start instruction trace */
//              con_dev.dctrl |= DEBUG_CMD;
//              sim_debug(DEBUG_INST, &cpu_dev, "|con_srvo DXP> received|\n");
                sim_debug(DEBUG_INST, &cpu_dev, "|con_srvo DEFA received|\n");
            }
#endif
            sim_putchar(ch);                /* output next char to device */
            if (isprint(ch))
                sim_debug(DEBUG_CMD, dptr,
                "con_srvo write addr %06x chsa %04x cmd %02x byte %d = %02x [%c]\n",
                mema, chsa, cmd, cnt, ch, ch);
            else
                sim_debug(DEBUG_CMD, dptr,
                "con_srvo write addr %06x chsa %04x cmd %02x byte %d = %02x\n",
                mema, chsa, cmd, cnt, ch);
            mema = chp->ccw_addr;           /* get next buffer addr */
            cnt++;                          /* count chars output */
        }
        /* write is complete, post status */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvo write CMD %08x chsa %04x cmd %02x complete\n",
            uptr->CMD, chsa, cmd);
        uptr->CMD &= LMASK;                 /* nothing left, command complete */
/*RTC*/ outbusy = 0;                        /* output done */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        break;
#endif
    }
    return SCPE_OK;
}

/* Handle input transfers for console */
t_stat con_srvi(UNIT *uptr) {
    DEVICE      *dptr = uptr->dptr;
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - con_unit);   /* unit 0 is read, unit 1 is write */
    int         cmd = uptr->CMD & CON_MSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */
    int         len = chp->ccw_count;       /* INCH command count */
    uint32      mema = chp->ccw_addr;       /* get inch or buffer addr */
    uint32      tstart;
    uint8       ch;
    t_stat      r;
    int32       wait_time=10000;

    switch (cmd) {

    /* if output tried to input device, error */
    case CON_RWD:       /* 0x37 */          /* TOF and write line */
    case CON_WR:        /* 0x01 */          /* Write command */
        /* if input requested for output device, give error */
        uptr->SNS |= SNS_CMDREJ;            /* command rejected */
        uptr->CMD &= LMASK;                 /* nothing left, command complete */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvi Write to input device CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_UNITCHK); /* unit check */
        break;

    case CON_INCH2:      /* 0xf0 */         /* INCH command */
        uptr->CMD &= LMASK;                 /* nothing left, command complete */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvi INCH unit %02x: CMD %08x cmd %02x incnt %02x u4 %02x inch %06x\n",
            unit, uptr->CMD, cmd, con_data[unit].incnt, uptr->u4, mema);

        /* now call set_inch() function to write and test inch buffer addresses */
        tstart = set_inch(uptr, mema, 128); /* new address & 128 entries */
        if ((tstart == SCPE_MEM) || (tstart == SCPE_ARG)) { /* any error */
            /* we have error, bail out */
            uptr->SNS |= SNS_CMDREJ;
            sim_debug(DEBUG_CMD, dptr,
                "con_srvi INCH Error unit %02x: CMD %08x cmd %02x incnt %02x u4 %02x\n",
                unit, uptr->CMD, cmd, con_data[unit].incnt, uptr->u4);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        con_data[unit].incnt = 0;           /* buffer empty */
        uptr->u4 = 0;                       /* no I/O yet */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvi INCH CMD %08x chsa %04x len %02x inch %06x\n", uptr->CMD, chsa, len, mema);
        /* WARNING, if SNS_DEVEND is not set, diags fail by looping in CON diag */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        /* drop through to poll input */
        break;

    case CON_NOP:       /* 0x03 */          /* NOP has do nothing */
        uptr->CMD &= ~CON_MSK;              /* remove old CMD */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvi NOP CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        /* drop through to poll input */
        break;

    case CON_CON:       /* 0x1f */          /* Connect, return Data Set ready */
        uptr->SNS |= (SNS_DSR|SNS_DCD);     /* Data set ready, Data Carrier detected */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvi CON CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
        uptr->CMD &= ~CON_MSK;              /* remove old CMD */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case CON_DIS:       /* 0x23 */          /* Disconnect has do nothing */
        uptr->SNS &= ~(SNS_DSR|SNS_DCD);    /* Data set not ready */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvi DIS CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
        uptr->CMD &= ~CON_MSK;              /* remove old CMD */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case CON_SNS:       /* 0x04 */          /* Sense */
        sim_debug(DEBUG_CMD, dptr,
            "con_srvi cmd %04x: Cmd Sense %02x\n", chsa, uptr->SNS);
        /* value 4 is Data Set Ready */
        /* value 5 is Data carrier detected n/u */
        ch = uptr->SNS & 0xff;              /* Sense byte 3 */
        if (chan_write_byte(chsa, &ch)) {   /* write byte to memory */
            /* write error */
            cmd = 0;                        /* no cmd now */
            sim_debug(DEBUG_CMD, dptr,
                "con_srvi write error unit %02x: CMD %08x read %02x u4 %02x ccw_count %02x\n",
                unit, uptr->CMD, ch, uptr->u4, chp->ccw_count);
            uptr->CMD &= LMASK;             /* nothing left, command complete */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
            break;
        }
        uptr->CMD &= LMASK;                 /* nothing left, command complete */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
        break;

    case CON_ECHO:      /* 0x0a */          /* read from device w/ECHO */
        uptr->CMD |= CON_EKO;               /* save echo status */
    case CON_RD:        /* 0x02 */          /* read from device */
    case CON_RDBWD:     /* 0x0c */          /* Read Backward */

        if ((uptr->u4 != con_data[unit].incnt) ||  /* input empty */
            (uptr->CMD & CON_INPUT)) {      /* input waiting? */
            ch = con_data[unit].ibuff[uptr->u4]; /* get char from read buffer */
            if (isprint(ch))
                sim_debug(DEBUG_IRQ, dptr,
                "con_srvi readbuf unit %02x: CMD %08x read %02x [%c] incnt %02x u4 %02x len %02x\n",
                unit, uptr->CMD, ch, ch, con_data[unit].incnt, uptr->u4, chp->ccw_count);
            else
                sim_debug(DEBUG_IRQ, dptr,
                "con_srvi readbuf unit %02x: CMD %08x read %02x incnt %02x u4 %02x len %02x\n",
                unit, uptr->CMD, ch, con_data[unit].incnt, uptr->u4, chp->ccw_count);
#ifdef DO_DYNAMIC_DEBUG
        /* turn on instruction trace */
        cpu_dev.dctrl |= DEBUG_INST;        /* start instruction trace */
#endif

            /* process any characters */
            if (uptr->u4 != con_data[unit].incnt) { /* input available */
                ch = con_data[unit].ibuff[uptr->u4];    /* get char from read buffer */
                /* this fixes mpx1x time entry on startup */
                if (uptr->CMD & CON_EKO)    /* ECHO requested */
                    sim_putchar(ch);        /* ECHO the char */
                if (chan_write_byte(chsa, &ch)) {   /* write byte to memory */
                    /* write error */
                    cmd = 0;                /* no cmd now */
                    sim_debug(DEBUG_CMD, dptr,
                        "con_srvi write error unit %02x: CMD %08x read %02x u4 %02x ccw_count %02x\n",
                        unit, uptr->CMD, ch, uptr->u4, chp->ccw_count);
                    uptr->CMD &= ~CON_MSK;  /* remove old CMD */
                    uptr->CMD &= ~CON_INPUT;    /* input waiting? */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                    break;
                }
                /* character accepted, bump buffer pointer */
                uptr->u4++;                 /* next char position */

                sim_debug(DEBUG_CMD, dptr,
                    "con_srvi write to mem unit %02x: CMD %08x read %02x u4 %02x incnt %02x\n",
                    unit, uptr->CMD, ch, uptr->u4, con_data[unit].incnt);

                /* see if at end of buffer */
                if (uptr->u4 >= (int32)sizeof(con_data[unit].ibuff))
                    uptr->u4 = 0;           /* reset pointer */

                /* user want more data? */
                if ((test_write_byte_end(chsa)) == 0) {
                    sim_debug(DEBUG_CMD, dptr,
                        "con_srvi need more unit %02x CMD %08x u4 %02x ccw_count %02x incnt %02x\n",
                        unit, uptr->CMD, uptr->u4, chp->ccw_count, con_data[unit].incnt);
                    /* user wants more, look next time */
                    if (uptr->u4 == con_data[unit].incnt) { /* input empty */
                        uptr->CMD &= ~CON_INPUT;    /* no input available */
                    }
//                  wait_time = 200;        /* process next time */
//                  wait_time = 400;        /* process next time */
                    wait_time = 800;        /* process next time */
                    break;
                }
                /* command is completed */
                if (isprint(ch))
                    sim_debug(DEBUG_CMD, dptr,
                    "con_srvi read done unit %02x CMD %08x read %02x [%c] u4 %02x ccw_count %02x incnt %02x\n",
                    unit, uptr->CMD, ch, ch, uptr->u4, chp->ccw_count, con_data[unit].incnt);
                else
                    sim_debug(DEBUG_CMD, dptr,
                    "con_srvi read done unit %02x CMD %08x read %02x u4 %02x ccw_count %02x incnt %02x\n",
                    unit, uptr->CMD, ch, uptr->u4, chp->ccw_count, con_data[unit].incnt);
#ifdef DO_DYNAMIC_DEBUG
        /* turn on instruction trace */
        cpu_dev.dctrl |= DEBUG_INST;        /* start instruction trace */
#endif
                cmd = 0;                    /* no cmd now */
                uptr->CMD &= LMASK;         /* nothing left, command complete */
                if (uptr->u4 != con_data[unit].incnt) { /* input empty */
                    uptr->CMD |= CON_INPUT; /* input still available */
                }
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                break;
            }
            break;
        }
    default:
        break;
    }

    /* check for next input if reading or @@A sequence */
    r = sim_poll_kbd();                     /* poll for a char */
    if (r & SCPE_KFLAG) {                   /* got a char */
        ch = r & 0xff;                      /* drop any extra bits */
        if ((uptr->CMD & CON_READ)) {       /* looking for input? */
            atbuf = 0;                      /* reset attention buffer */
            uptr->CMD &= ~CON_ATAT;         /* no @@A input */
            if (ch == '@') {                /* maybe for console int */
                atbuf = (ch)<<8;            /* start anew */
                uptr->CMD |= CON_ATAT;      /* show getting @ */
            }
#ifndef TEST4MPX
            if (ch == '\n')                 /* convert newline */
                ch = '\r';                  /* make newline into carriage return */ 
#endif
            if (isprint(ch))
                sim_debug(DEBUG_CMD, dptr,
                "con_srvi handle readch unit %02x: CMD %08x read %02x [%c] u4 %02x incnt %02x r %x\n",
                unit, uptr->CMD, ch, ch, uptr->u4, con_data[unit].incnt, r);
            else
                sim_debug(DEBUG_CMD, dptr,
                "con_srvi handle readch unit %02x: CMD %08x read %02x u4 %02x incnt %02x r %x\n",
                unit, uptr->CMD, ch, uptr->u4, con_data[unit].incnt, r);
#ifdef DO_DYNAMIC_DEBUG
        /* turn on instruction trace */
        cpu_dev.dctrl |= DEBUG_INST;        /* start instruction trace */
#endif

            /* put char in buffer */
            con_data[unit].ibuff[con_data[unit].incnt++] = ch;

            /* see if count at max, if so reset to start */
            if (con_data[unit].incnt >= sizeof(con_data[unit].ibuff))
                con_data[unit].incnt = 0;   /* reset buffer cnt */

            uptr->CMD |= CON_INPUT;         /* we have a char available */
            if (isprint(ch))
                sim_debug(DEBUG_CMD, dptr,
                "con_srvi readch unit %02x: CMD %08x read %02x [%c] u4 %02x incnt %02x\n",
                unit, uptr->CMD, ch, ch, uptr->u4, con_data[unit].incnt);
            else
                sim_debug(DEBUG_CMD, dptr,
                "con_srvi readch unit %02x: CMD %08x read %02x u4 %02x incnt %02x\n",
                unit, uptr->CMD, ch, uptr->u4, con_data[unit].incnt);
            sim_activate(uptr, 30);         /* do this again */
//01172021  sim_activate(uptr, 400);        /* do this again */
//          sim_activate(uptr, 800);        /* do this again */
            return SCPE_OK;
        }
        /* not looking for input, look for attn or wakeup */
        if (ch == '?') {
            /* set ring bit? */
            set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);  /* tell user */
        }     
        /* not wanting input, but we have a char, look for @@A */
        if (uptr->CMD & CON_ATAT) {         /* looking for @@A */
            /* we have at least one @, look for another */
            if (ch == '@' || ch == 'A' || ch == 'a') {
                uint8 cc = ch;
                if (cc == 'a')
                    cc = 'A';               /* make uppercase */
                sim_putchar(ch);            /* ECHO the char */
                atbuf = (atbuf|cc)<<8;      /* merge new char */
                if (atbuf == 0x40404100) {
                    attention_trap = CONSOLEATN_TRAP;   /* console attn (0xb4) */
                    atbuf = 0;              /* reset attention buffer */
                    uptr->CMD &= ~CON_ATAT; /* no @@A input */
                    sim_putchar('\r');      /* return char */
                    sim_putchar('\n');      /* line feed char */
                    sim_debug(DEBUG_CMD, dptr,
                        "con_srvi unit %02x: CMD %08x read @@A Console Trap\n", unit, uptr->CMD);
                    uptr->u4 = 0;           /* no input count */
                    con_data[unit].incnt = 0;   /* no input data */
                }
//              sim_activate(uptr, wait_time);  /* do this again */
                sim_activate(uptr, 400);    /* do this again */
//              sim_activate(uptr, 4000);   /* do this again */
                return SCPE_OK;
            }
            /* char not for us, so keep looking */
            atbuf = 0;                      /* reset attention buffer */
            uptr->CMD &= ~CON_ATAT;         /* no @@A input */
        }
        /* not looking for input, look for attn or wakeup */
        if (ch == '@') {
            atbuf = (atbuf|ch)<<8;          /* merge in char */
            uptr->CMD |= CON_ATAT;          /* show getting @ */
            sim_putchar(ch);                /* ECHO the char */
        }
        /* assume it is for next read request, so save it */
        /* see if count at max, if so reset to start */
        if (con_data[unit].incnt >= sizeof(con_data[unit].ibuff))
            con_data[unit].incnt = 0;       /* reset buffer cnt */

        /* put char in buffer */
        con_data[unit].ibuff[con_data[unit].incnt++] = ch;

        uptr->CMD |= CON_INPUT;             /* we have a char available */
        if (isprint(ch))
            sim_debug(DEBUG_CMD, dptr,
            "con_srvi readch2 unit %02x: CMD %08x read %02x [%c] u4 %02x incnt %02x r %x\n",
            unit, uptr->CMD, ch, ch, uptr->u4, con_data[unit].incnt, r);
        else
            sim_debug(DEBUG_CMD, dptr,
            "con_srvi readch2 unit %02x: CMD %08x read %02x u4 %02x incnt %02x r %x\n",
            unit, uptr->CMD, ch, uptr->u4, con_data[unit].incnt, r);
#ifdef DO_DYNAMIC_DEBUG
        /* turn off debug trace because we are already hung */
        sim_debug(DEBUG_INST, &cpu_dev, "con_srvi readch3 stopping debug\n");
        cpu_dev.dctrl &= ~(DEBUG_INST|DEBUG_TRAP|DEBUG_IRQ); /* start instruction trace */
        con_dev.dctrl &= ~(DEBUG_XIO|DEBUG_CMD);
#endif
    }
    sim_activate(uptr, wait_time);          /* do this again */
    return SCPE_OK;
}

t_stat  con_reset(DEVICE *dptr) {
    tmxr_set_console_units (&con_unit[0], &con_unit[1]);
    return SCPE_OK;
}

/* Handle rschnlio cmds for console */
t_stat  con_rschnlio(UNIT *uptr) {
    uint16  chsa = GET_UADDR(uptr->CMD);
    int     cmd = uptr->CMD & CON_MSK;
    con_ini(uptr, 0);                       /* reset the unit */
    sim_debug(DEBUG_EXP, &con_dev, "con_rschnl chsa %04x cmd = %02x\n", chsa, cmd);
//  cpu_dev.dctrl |= (DEBUG_INST|DEBUG_TRAP|DEBUG_IRQ); /* start instruction trace */
    return SCPE_OK;
}

/* Handle haltio transfers for console */
t_stat  con_haltio(UNIT *uptr) {
    uint16  chsa = GET_UADDR(uptr->CMD);
    int     cmd = uptr->CMD & CON_MSK;
    int     unit = (uptr - con_unit);       /* unit # 0 is read, 1 is write */
    CHANP   *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_EXP, &con_dev,
        "con_haltio enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* terminate any input command */
    /* UTX wants SLI bit, but no unit exception */
    /* status must not have an error bit set */
    /* otherwise, UTX will panic with "bad status" */
    if ((uptr->CMD & CON_MSK) != 0) {       /* is unit busy */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_haltio HIO chsa %04x cmd = %02x ccw_count %02x\n",
            chsa, cmd, chp->ccw_count);
        sim_cancel(uptr);                   /* stop timer */
        /* stop any I/O and post status and return error status */
        chp->ccw_count = 0;                 /* zero the count */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);   /* reset chaining bits */
        uptr->CMD &= LMASK;                 /* make non-busy */
        uptr->u4 = 0;                       /* no I/O yet */
        con_data[unit].incnt = 0;           /* no input data */
        uptr->SNS = SNS_RDY|SNS_ONLN;       /* status is online & ready */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_haltio HIO I/O stop chsa %04x cmd = %02x\n", chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* force end */
        return CC2BIT | SCPE_IOERR;         /* tell chan code to post status */
    }
    uptr->CMD &= LMASK;                     /* make non-busy */
    uptr->SNS = SNS_RDY|SNS_ONLN;           /* status is online & ready */
    sim_debug(DEBUG_CMD, &con_dev,
        "con_haltio HIO not busy chsa %04x cmd = %02x ccw_count %02x\n",
        chsa, cmd, chp->ccw_count);
    return CC1BIT | SCPE_OK;                /* not busy */
}
#endif

