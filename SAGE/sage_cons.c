/* sage_sio.c: serial devices for sage-II system

   Copyright (c) 2009-2010 Holger Veit

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
   Holger Veit BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Holger Veit et al shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Holger Veit et al.

   12-Oct-09    HV      Initial version
   24-Jul-10    HV      Added TMXR code to attach CONS and SIO to network
*/

#include "sim_defs.h"
#include "sim_timer.h"
#include "sage_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#define SIOPOLL 0
#define SIOTERM 1

#define SIO_POLL_FIRST      1       /* immediate */
#define SIO_POLL_RATE       100     /* sample 100 times /sec */
#define SIO_POLL_WAIT       15800   /* about 10ms */
#define SIO_OUT_WAIT        200

static t_stat sio_reset(DEVICE* dptr);
static t_stat sioterm_svc(UNIT*);
static t_stat siopoll_svc(UNIT*);
static t_stat sio_attach(UNIT*, CONST char*);
static t_stat sio_detach(UNIT*);
static t_stat sio_txint(I8251* chip);
static t_stat sio_rxint(I8251* chip);
extern DEVICE sagesio_dev;

UNIT sio_unit[] = {
    { UDATA (&siopoll_svc, UNIT_ATTABLE, 0), SIO_POLL_WAIT },
    { UDATA (&sioterm_svc, UNIT_IDLE, 0), SIO_OUT_WAIT }
};

static SERMUX sio_mux = {
        SIO_POLL_FIRST,      /*pollfirst*/
        SIO_POLL_RATE,       /*pollrate*/
        { 0 },               /*ldsc*/
        { 1, 0, 0, 0 },      /*desc*/
        &sio_unit[SIOTERM],  /*term_unit*/
        &sio_unit[SIOPOLL]   /*poll unit*/
};

static I8251 u58 = { 
        {0,0,U58_ADDR,4,2},
        &sagesio_dev,NULL,NULL,i8251_reset,
        &sio_txint,&sio_rxint,
        &sio_unit[SIOPOLL],&sio_unit[SIOTERM],
        &sio_mux
};

REG sio_reg[] = {
    { DRDATA(INIT, u58.init, 3) },
    { HRDATA(MODE, u58.mode, 8) },
    { HRDATA(SYNC1, u58.sync1, 8) },
    { HRDATA(SYNC2, u58.sync2, 8) },
    { HRDATA(CMD, u58.cmd, 8) },
    { HRDATA(IBUF, u58.ibuf, 8) },
    { HRDATA(OBUF, u58.obuf, 8) },
    { HRDATA(STATUS, u58.status, 8) },
    { HRDATA(STATUS, u58.bitmask, 8), REG_HRO },
    { 0 }
};

static MTAB sio_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "IO", "IO", &set_iobase, &show_iobase, NULL },
    { 0 }
};

DEVICE sagesio_dev = {
    "SIO", sio_unit, sio_reg, sio_mod,
    2, 16, 32, 2, 16, 16,
    NULL, NULL, &sio_reset,
    NULL, &sio_attach, &sio_detach,
    &u58, DEV_DEBUG, 0,
    i8251_dt, NULL, NULL
};

static t_stat sioterm_svc(UNIT* uptr)
{
    DEVICE* dptr = find_dev_from_unit(uptr);
    I8251* chip = (I8251*)dptr->ctxt;
    SERMUX* mux = chip->mux;
    t_stat rc;
    int ch = chip->obuf;
    
    /* suppress NUL bytes after CR LF */
    switch (ch) {
    case 0x0d:
        chip->crlf = 1; break;
    case 0x0a:
        chip->crlf = chip->crlf==1 ? 2 : 0; break;
    case 0:
        if (chip->crlf==2) goto set_stat;
    default:
        chip->crlf = 0;
    }
    
    /* TODO? sim_tt_outcvt */
    
    /* attached to a telnet port? */
    if (mux->poll->flags & UNIT_ATT) {
        if ((rc=tmxr_putc_ln(&mux->ldsc, ch & chip->bitmask)) != SCPE_OK) {
            sim_activate(uptr, uptr->wait);
            return SCPE_OK;
        } else
            tmxr_poll_tx(&mux->desc);
    } else {
        /* no, use normal terminal output */
        if ((rc=sim_putchar_s(ch & chip->bitmask)) != SCPE_OK) {
            sim_activate(uptr, uptr->wait);
            return rc==SCPE_STALL ? SCPE_OK : rc;
        }
    }
set_stat:
    chip->status |= I8251_ST_TXEMPTY;
    if (chip->cmd & I8251_CMD_TXEN) {
        chip->status |= I8251_ST_TXRDY;
        return sio_txint(chip);
    }
    chip->status &= ~I8251_ST_TXRDY;
    return SCPE_OK;
}

static t_stat siopoll_svc(UNIT* uptr)
{
    int32 c;
    DEVICE* dptr = find_dev_from_unit(uptr);
    I8251* chip = (I8251*)dptr->ctxt;
    SERMUX* mux = chip->mux;
    
    sim_activate(uptr, uptr->wait); /* restart it again */
    
    /* network attached? */
    if (mux->poll->flags & UNIT_ATT) {
        if (tmxr_poll_conn(&mux->desc) >= 0) /* new connection? */
            mux->ldsc.rcve = 1;
        tmxr_poll_rx(&mux->desc);
        if (!tmxr_rqln(&mux->ldsc)) return SCPE_OK;
        /* input ready */
        c = tmxr_getc_ln(&mux->ldsc);
        if ((c & TMXR_VALID)==0) return SCPE_OK;
        c &= 0xff; /* extract character */
    } else
        return SCPE_OK;
        
    if (!(chip->cmd & I8251_CMD_RXE)) { /* ignore data if receiver not enabled */
        chip->status &= ~I8251_ST_RXRDY;
        return SCPE_OK;
    }

    /* got char */
    if (c & SCPE_BREAK) {   /* a break? */
        c = 0;
        chip->status |= I8251_ST_SYNBRK;
    } else
        chip->status &= ~I8251_ST_SYNBRK;

    /* TODO? sim_tt_icvt */
    chip->ibuf = c & chip->bitmask;
    if (chip->status & I8251_ST_RXRDY)
        chip->status |= I8251_ST_OE;
    chip->status |= I8251_ST_RXRDY;
    return sio_rxint(chip);
}

static t_stat sio_reset(DEVICE* dptr) 
{
    t_stat rc;
    I8251* chip = (I8251*)dptr->ctxt;
    SERMUX* mux = chip->mux;
    
    if ((rc = (dptr->flags & DEV_DIS) ?
        del_iohandler(chip) :
        add_iohandler(mux->poll,chip,i8251_io)) != SCPE_OK) return rc;

    u58.reset(&u58);
    mux->term->wait = 1000; /* TODO adjust to realistic speed */

    /* network attached? */
    if (mux->poll->flags & UNIT_ATT) {
        mux->poll->wait = mux->pfirst;
        sim_activate(mux->poll,mux->poll->wait); /* start poll routine */
    } else
        sim_cancel(mux->poll);
    sim_cancel(mux->term);
    return SCPE_OK;
}

static t_stat sio_attach(UNIT* uptr, CONST char* cptr)
{
    return mux_attach(uptr,cptr,&sio_mux);
}

static t_stat sio_detach(UNIT* uptr)
{
    return mux_detach(uptr,&sio_mux);
}

static t_stat sio_txint(I8251* chip)
{
    TRACE_PRINT0(DBG_UART_IRQ,"Raise TX Interrupt");
    return sage_raiseint(SIOTX_PICINT);
}

static t_stat sio_rxint(I8251* chip)
{
    TRACE_PRINT0(DBG_UART_IRQ,"Raise RX Interrupt");
    return sage_raiseint(SIORX_PICINT);
}

/***************************************************************************************************/

#define CONSPOLL    0
#define CONSTERM    1

#define CONS_POLL_FIRST     1       /* immediate */
#define CONS_POLL_RATE      100     /* sample 100 times /sec */
#define CONS_POLL_WAIT      15800   /* about 10ms */
#define CONS_OUT_WAIT       200

static t_stat cons_reset(DEVICE* dptr);
static t_stat cons_txint(I8251* chip);
static t_stat cons_rxint(I8251* chip);
static t_stat conspoll_svc(UNIT*);
static t_stat consterm_svc(UNIT*);
static t_stat cons_attach(UNIT*, CONST char*);
static t_stat cons_detach(UNIT*);
extern DEVICE sagecons_dev;

UNIT cons_unit[] = {
    { UDATA (&conspoll_svc, UNIT_ATTABLE, 0), CONS_POLL_WAIT },
    { UDATA (&consterm_svc, UNIT_IDLE, 0), CONS_OUT_WAIT }
};

static SERMUX cons_mux = {
        CONS_POLL_FIRST,
        CONS_POLL_RATE,
        { 0 },
        { 1, 0, 0, 0 },
        &cons_unit[CONSTERM],
        &cons_unit[CONSPOLL]
};

static I8251 u57 = {
        { 0,0,U57_ADDR,4,2},
        &sagecons_dev,NULL,NULL,&i8251_reset,
        &cons_txint,&cons_rxint,
        &cons_unit[CONSPOLL],&cons_unit[CONSTERM],
        &cons_mux
};

REG cons_reg[] = {
    { DRDATA(INIT, u57.init, 3) },
    { HRDATA(MODE, u57.mode, 8) },
    { HRDATA(SYNC1, u57.sync1, 8) },
    { HRDATA(SYNC2, u57.sync2, 8) },
    { HRDATA(CMD, u57.cmd, 8) },
    { HRDATA(IBUF, u57.ibuf, 8) },
    { HRDATA(OBUF, u57.obuf, 8) },
    { HRDATA(STATUS, u57.status, 8) },
    { HRDATA(BITS, u57.bitmask,8), REG_HRO },
    { 0 }
};

static MTAB cons_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,  "IO",       "IO",       &set_iobase, &show_iobase, NULL },
    { 0 }
};

DEVICE sagecons_dev = {
    "CONS", cons_unit, cons_reg, cons_mod,
    2, 16, 32, 2, 16, 16,
    NULL, NULL, &cons_reset,
    NULL, &cons_attach, &cons_detach,
    &u57, DEV_DEBUG, 0,
    i8251_dt, NULL, NULL
};

static t_stat cons_reset(DEVICE* dptr) 
{
    t_stat rc;
    int32 wait;
    I8251* chip = (I8251*)dptr->ctxt;
    SERMUX* mux = chip->mux;
    
    if ((rc = (dptr->flags & DEV_DIS) ?
        del_iohandler(chip) :
        add_iohandler(mux->poll,chip,&i8251_io)) != SCPE_OK) return rc;

    u57.reset(&u57);
    
    /* initialize POLL timer */
    wait = mux->poll->wait = CONS_POLL_WAIT;
    sim_rtcn_init(wait, TMR_CONS);
    
    u57.oob = TRUE; /* this is the console */
    sim_activate(mux->poll, wait);
    sim_cancel(mux->term);
    return SCPE_OK;
}

/* this service is started when a unit is attached, or characters are available on keyboard */
static t_stat conspoll_svc(UNIT* uptr)
{
    int32 c, kbdc;
    DEVICE* dptr = find_dev_from_unit(uptr);
    I8251* chip = (I8251*)dptr->ctxt;
    SERMUX* mux = chip->mux;

    uptr->wait = sim_rtcn_calb(mux->prate, TMR_CONS); /* calibrate timer */
    sim_activate(uptr, uptr->wait); /* restart it again */

    kbdc = sim_poll_kbd(); /* check keyboard */
    if (kbdc==SCPE_STOP) return kbdc; /* handle CTRL-E */

    /* network-redirected input? */
    if (mux->poll->flags & UNIT_ATT) {
        if (tmxr_poll_conn(&mux->desc) >= 0) /* incoming connection */
            mux->ldsc.rcve = 1;

        tmxr_poll_rx(&mux->desc); /* poll for input */
        if (!tmxr_rqln(&mux->ldsc)) return SCPE_OK;
        /* input ready */
        c = tmxr_getc_ln(&mux->ldsc);
        if ((c & TMXR_VALID)==0) return SCPE_OK;
        c &= 0xff; /* extract character */
    } else {
        c = kbdc; /* use char polled from keyboard instead */
        if (c < SCPE_KFLAG) return c;       /* ignore data if not valid */
    }
    
    if (!(chip->cmd & I8251_CMD_RXE)) { /* ignore data if receiver not enabled */
        chip->status &= ~I8251_ST_RXRDY;
        return SCPE_OK;
    }
    
    /* got char */
    if (c & SCPE_BREAK) {   /* a break? */
        c = 0;
        chip->status |= I8251_ST_SYNBRK;
    } else
        chip->status &= ~I8251_ST_SYNBRK;

    /* TODO? sim_tt_icvt */
    chip->ibuf = c & chip->bitmask;
    if (chip->status & I8251_ST_RXRDY)
        chip->status |= I8251_ST_OE;
    chip->status |= I8251_ST_RXRDY;
    return cons_rxint(chip);
}

static t_stat consterm_svc(UNIT* uptr)
{
    DEVICE* dptr = find_dev_from_unit(uptr);
    I8251* chip = (I8251*)dptr->ctxt;
    SERMUX* mux = chip->mux;
    t_stat rc;

    int ch = chip->obuf;
    
    /* suppress NUL bytes after CR LF */
    switch (ch) {
    case 0x0d:
        chip->crlf = 1; break;
    case 0x0a:
        chip->crlf = (chip->crlf==1) ? 2 : 0; break;
    case 0:
        if (chip->crlf==2) goto set_stat;
    default:
        chip->crlf = 0;
    }
    
    /* TODO? sim_tt_outcvt */
    
    /* attached to a telnet port? */
    if (mux->poll->flags & UNIT_ATT) {
        if ((rc=tmxr_putc_ln(&mux->ldsc, ch & chip->bitmask)) != SCPE_OK) {
            sim_activate(uptr, uptr->wait);
            return SCPE_OK;
        } else
            tmxr_poll_tx(&mux->desc);
    } else {
        /* no, use normal terminal output */
        if ((rc=sim_putchar_s(ch & chip->bitmask)) != SCPE_OK) {
            sim_activate(uptr, uptr->wait);
            return rc==SCPE_STALL ? SCPE_OK : rc;
        }
    }
set_stat:
    chip->status |= I8251_ST_TXEMPTY;
    if (chip->cmd & I8251_CMD_TXEN) {
        chip->status |= I8251_ST_TXRDY;
        return cons_txint(chip);
    }
    chip->status &= ~I8251_ST_TXRDY;
    return SCPE_OK;
}

static t_stat cons_txint(I8251* chip)
{
    TRACE_PRINT0(DBG_UART_IRQ,"Raise TX Interrupt");
    return sage_raiseint(CONSTX_PICINT);
}

static t_stat cons_rxint(I8251* chip)
{
    TRACE_PRINT0(DBG_UART_IRQ,"Raise RX Interrupt");
    return m68k_raise_autoint(CONSRX_AUTOINT);
}

static t_stat cons_attach(UNIT* uptr, CONST char* cptr)
{
    return mux_attach(uptr,cptr,&cons_mux);
}

static t_stat cons_detach(UNIT* uptr)
{
    return mux_detach(uptr,&cons_mux);
}

t_stat mux_attach(UNIT* uptr, CONST char* cptr, SERMUX* mux)
{
    t_stat rc;

    mux->desc.ldsc = &mux->ldsc;
    if ((rc = tmxr_attach(&mux->desc, uptr, cptr)) == SCPE_OK) {
        mux->poll->wait = mux->pfirst;
        sim_activate(mux->poll,mux->poll->wait);
    }
    return rc;
}

t_stat mux_detach(UNIT* uptr,SERMUX* mux)
{
    t_stat rc = tmxr_detach(&mux->desc, uptr);
    mux->ldsc.rcve = 0;
    sim_cancel(mux->poll);
    sim_cancel(mux->term);
    return rc;
}
