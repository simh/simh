/* PDQ3_stddev.c: PDQ3 simulator standard devices

   Work derived from Copyright (c) 2004-2012, Robert M. Supnik
   Copyright (c) 2013 Holger Veit

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

   Except as contained in this notice, the names of Robert M Supnik and Holger Veit 
   shall not be used in advertising or otherwise to promote the sale, use or 
   other dealings in this Software without prior written authorization from
   Robert M Supnik and Holger Veit.
   
   2013xxxx hv initial version
   20130902 hv added telnet multiplexer code
   20131020 hv fixed CON interrupt handling
   20131103 hv connect CON_ATTACH logic with DSR, so that DSR is set if tcp connect
   20141003 hv compiler suggested warnings (vc++2013, gcc)
*/
#include "pdq3_defs.h"
#include <ctype.h>

extern UNIT cpu_unit;
extern UNIT con_unit[];

static t_stat con_termsvc(UNIT *uptr);
static t_stat con_pollsvc(UNIT *uptr);
static t_stat con_reset(DEVICE* dptr);

static t_stat tim_reset(DEVICE *dptr);
static t_stat tim0_svc(UNIT* uptr);
static t_stat tim1_svc(UNIT* uptr);
static t_stat tim2_svc(UNIT* uptr);

/* CON USART registers */
/* This is described as positive logic, and represents the 
 * content of the corresponding registers.
 * However, the USART is connected to inverted DAL lines
 * and needs to be written to with the inverted value
 * This is done in CPU Read/Write */
#define CONC1_LOOP 0x80 /* 0=loopmode diagnostic, 1=normal full duplex */
#define CONC1_BRK  0x40 /* 1=send break */
#define CONC1_MISC 0x20 /* 1=one stop bit, 0=two */
#define CONC1_ECHO 0x10 /* 1=echo received data */
#define CONC1_PE   0x08 /* 1=check parity */
#define CONC1_RE   0x04 /* 1=enable receiver */
#define CONC1_RTS  0x02 /* 1=enable transmitter if CTS */
#define CONC1_DTR  0x01 /* 1=enable CD, DSR,RI interrupts */
static uint8 con_ctrl1;
#define CONC2_CLENMASK 0xc0 /* number of bits */
#define  CONC2_CLEN8   0x00
#define  CONC2_CLEN7   0x40
#define  CONC2_CLEN6   0x80
#define  CONC2_CLEN5   0xc0
#define CONC2_MODE     0x20 /* not used set to 0 (async mode) */
#define CONC2_ODDEVN   0x10 /* 0=even parity */
#define CONC2_RXCLK    0x08 /* not used, set to 1 */
#define CONC2_CLKMASK  0x07 /* clock selector */
#define  CONC2_CLK110  0x06 /* must be set to 001 (rate 1 clock) */
static uint8 con_ctrl2;
#define CONS_DSC    0x80 /* set to 1 after status read, cleared by DSR/DCD/RI */
#define CONS_DSR    0x40 /* DSR input */
#define CONS_CD     0x20 /* DCD input */
#define CONS_FE     0x10 /* 1=framing error */
#define CONS_PE     0x08 /* 1=parity error */
#define CONS_OE     0x04 /* 1= overrun error */
#define CONS_DR     0x02 /* set to 1 if data received, 0 if data read */
#define CONS_THRE   0x01 /* set to 1 if data xmit buffer empty */
static uint8 con_status;
static uint8 con_xmit;
static uint8 con_rcv;

/************************************************************************************************
 * Onboard Console
 ***********************************************************************************************/

/* con data structures
   con_dev      con device descriptor
   con_unit     con unit descriptor
   con_mod      con modifier list
   con_reg      con register list
*/
IOINFO con_ioinfo1 = { NULL,         CON_IOBASE, 4, CON_RCV_VEC,  4, con_read, con_write };
IOINFO con_ioinfo2 = { &con_ioinfo1, 0,          0, CON_XMT_VEC,  3, con_read, con_write };
DEVCTXT con_ctxt = { &con_ioinfo2 };

UNIT con_unit[] = {
  { UDATA (&con_pollsvc, UNIT_ATTABLE, 0), CON_POLLRATE, },
  { UDATA (&con_termsvc, UNIT_IDLE, 0), CON_TERMRATE, }
};
static UNIT* con_tti = &con_unit[0]; /* shorthand for console input and output units */
static UNIT* con_tto = &con_unit[1];

REG con_reg[] = {
    { HRDATA (CTRL1,  con_ctrl1, 8) },
    { HRDATA (CTRL2,  con_ctrl2, 8) },
    { HRDATA (STAT,   con_status, 8) },
    { HRDATA (XMIT,   con_xmit, 8) },
    { HRDATA (RCV,    con_rcv, 8) },
    { NULL }
    };
MTAB con_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "IOBASE", "IOBASE", NULL, &show_iobase },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR", NULL, &show_iovec },
    { MTAB_XTD|MTAB_VDV, 0, "PRIO",   "PRIO",   NULL, &show_ioprio },
    { 0 }
    };
DEBTAB con_dflags[] = {
  { "WRITE", DBG_CON_WRITE },
  { "READ",  DBG_CON_READ },
  { "SVC",   DBG_CON_SVC },
  { 0, 0 }
};

DEVICE con_dev = {
    "CON",      /*name*/
    con_unit, /*units*/
    con_reg,   /*registers*/
    con_mod,   /*modifiers*/
    2,          /*numunits*/
    16,         /*aradix*/
    16,         /*awidth*/
    1,          /*aincr*/
    8,          /*dradix*/
    8,          /*dwidth*/
    NULL,       /*examine*/
    NULL,       /*deposit*/
    &con_reset, /*reset*/
    NULL,       /*boot*/
    NULL,       /*attach*/
    NULL,       /*detach*/
    &con_ctxt,  /*ctxt*/
    DEV_DEBUG|DEV_DISABLE,  /*flags*/
    0,          /*dctrl*/
    con_dflags, /*debflags*/
    NULL,       /*msize*/
    NULL        /*lname*/
};

/* bus reset handler */
t_stat con_binit() {
  con_status = CONS_THRE;
  setbit(con_status, CONS_DSR);
  
  con_ctrl1 = 0; /* no echo, receiver disabled, transmitter disabled */
  con_ctrl2 = 0; /* ASYNC mode, 8bits, Clock 1X */
  con_xmit = 0;
  con_rcv = 0;
  return SCPE_OK;
}

/* common handlers */
static t_stat con_reset(DEVICE* dptr)
{
  DEVCTXT* ctxt = (DEVCTXT*)dptr->ctxt;
  int32 wait = con_tti->wait = CON_POLLRATE;

  sim_rtcn_init (wait, TMR_CONPOLL); /* init poll timer */

  sim_cancel(con_tto); /* disable output service */
  
  /* register/deregister I/O handlers */
  if (dptr->flags & DEV_DIS) {
    del_ioh(ctxt->ioi);
  } else {
    add_ioh(ctxt->ioi);
    con_tti->buf = 0;
    sim_activate (con_tti, wait);
  }
  return con_binit();
}

t_stat con_attach(UNIT* uptr, char* cptr) {
  setbit(con_status, CONS_DSR|CONS_DSC);
  return SCPE_OK;
}

t_stat con_detach(UNIT* uptr) {
  clrbit(con_status, CONS_DSR);
  setbit(con_status, CONS_DSC);
  return SCPE_OK;
}

#define XMITENABLED() (isbitset(con_ctrl1,CONC1_RTS))
#define XMITENABLE() setbit(con_ctrl1,CONC1_RTS)
#define XMITDISABLE() clrbit(con_ctrl1,CONC1_RTS)
#define XMITEMPTY() (isbitset(con_status, CONS_THRE))

#define RCVENABLED() (isbitset(con_ctrl1,CONC1_RE))
#define RCVFULL() (isbitset(con_status, CONS_DR))
#define RCVENABLE() setbit(con_ctrl1,CONC1_RE)
#define RCVDISABLE() clrbit(con_ctrl1,CONC1_RE)

#define DSRACTIVE() (isbitset(con_ctrl1,CONC1_DTR) && isbitset(con_status,CONS_DSR))

/* The transmit interrupt is raised continuously,
 * as long as the transmit holding reg is empty and the transmitter is enabled.
 * It will be deasserted when the tranmit reg is full or tranmitter disabled.
 */
#define XMITINTR() cpu_assertInt(INT_CONT, XMITEMPTY())

/* The receive interrupt is raised continuously,
 * when the receiver holding register is full and the receiver is enabled.
 * it will be deasserted when the receiver reg is read or the receiver disabled.
 */
#define RCVINTR() cpu_assertInt(INT_CONR, RCVFULL())

/* The DSR interrupt is raised when DSC is set to 1 (pos logic)
 * and DTR is active, cleared if status is read */
#define DSRINTR() cpu_assertInt(INT_PRNT, DSRACTIVE())

/* Terminal output service */
t_stat con_termsvc (UNIT *uptr) {
  t_stat rc = SCPE_OK;

  int ch = sim_tt_outcvt(uptr->buf, TT_GET_MODE(uptr->flags));
  if (XMITENABLED()) { /* tranmitter enabled */
    if (ch >= 0) {
      if ((rc = sim_putchar_s(ch)) != SCPE_OK) {
        sim_activate(uptr, uptr->wait);   /* did not emit char, reschedule termsvc */
        return rc == SCPE_STALL ? SCPE_OK : rc;
      }
    }
  }
  uptr->pos = uptr->pos + 1;
  setbit(con_status, CONS_THRE); /* set transmitter holding reg empty */
  cpu_assertInt(INT_CONT, TRUE); /* generate an interrupt because of DRQO */
  return SCPE_OK;
}

/* Terminal input service */
t_stat con_pollsvc(UNIT *uptr) {
  int32 ch;

  uptr->wait = sim_rtcn_calb(CON_TPS, TMR_CONPOLL); /* calibrate timer */
  sim_activate(uptr, uptr->wait); /* restart polling */

  if ((ch = sim_poll_kbd()) < SCPE_KFLAG) /* check keyboard */
    return ch;
  uptr->buf = (ch & SCPE_BREAK) ? 0 : sim_tt_inpcvt(ch, TT_GET_MODE(uptr->flags));
  uptr->pos = uptr->pos + 1;

  if (RCVENABLED()) { /* receiver enabled? */
    if (RCVFULL()) /* handle data overrun */
      setbit(con_status, CONS_OE);

    con_rcv = ch & 0xff; /* put in receiver register */
    setbit(con_status, CONS_DR); /* notify: data received */
    cpu_assertInt(INT_CONR, TRUE); /* generate interrupt because of DRQI */

    if (isbitset(con_ctrl1, CONC1_ECHO)) { /* echo? XXX handle in telnet handler? */
      /* XXX use direct send here, not sending via con_termsvc */
      sim_putchar_s(ch);
    }
  }
  return SCPE_OK;
}

static int set_parity(int c, int odd)
{
  int i, p = 0;
  for (i=0; i<8; i++)
    if (c & (1<<i)) p ^= 1;
  c |= p ? 0 : 0x80;
  if (!odd) c ^= 0x80;
  return c;
}
#if 0
// currently unused
static int get_parity(int c, int even)
{
  int i, p = 0;
  for (i=0; i<8; i++)
    if (c & (1<<i)) p ^= 1;
  if (even) p ^= 1;
  return p;
}
#endif

// functions from memory handler to read and write a char
// note: the usart is connected to inverted data lines,
// this is fixed by negating input and output
//
// The logic in here uses the positive logic conventions as
// described in the WD1931 data sheet, not the ones in the PDQ-3_Hardware_Users_Manual
t_stat con_write(t_addr ioaddr, uint16 data) {

  /* note usart has inverted bus, so all data is inverted */
  data = (~data) & 0xff;
  switch (ioaddr & 0x0003) {
  case 0: /* CTRL1 */
    con_ctrl1 = data & 0xff;
    if (!RCVENABLED()) { /* disable receiver */
      clrbit(con_status,CONS_FE|CONS_PE|CONS_OE|CONS_DR);
      sim_cancel(con_tti);
    } else {
      sim_activate(con_tti, con_tti->wait); /* start poll service, will raise interrupt if buffer full */
    }
    if (!XMITENABLED()) { /* disable transmitter */
      /* will drain current pending xmit service. RTS output is assumed to become inactive
       * (it is not necessary to emulate it) */
    } else {
      if (XMITEMPTY()) {
      } else {
        /* some char in THR, start service to emit */
        sim_activate(con_tto, con_tto->wait);
      }
    }
    break;
  case 1:
    con_ctrl2 = data & 0xff;
    break;
  case 2:
    // ignore this here - DLE register
    break;
  case 3:
    switch (con_ctrl2 & CONC2_CLENMASK) {
    case CONC2_CLEN5: data &= 0x1f; break;
    case CONC2_CLEN6: data &= 0x3f; break;
    case CONC2_CLEN7: data &= 0x7f; 
      if (isbitset(con_ctrl1,CONC1_PE))
        data = set_parity(data, con_ctrl2 & CONC2_ODDEVN);
      break;
    case CONC2_CLEN8: data &= 0xff; break;
    }
    con_xmit = data & 0xff;
    con_tto->buf = data;
    clrbit(con_status,CONS_THRE);
    if (XMITENABLED())
      sim_activate(con_tto, con_tto->wait);
  }
//  RCVINTR();
  XMITINTR();
  DSRINTR();
  
  sim_debug(DBG_CON_WRITE, &con_dev, DBG_PCFORMAT0 "Byte write %02x (pos logic) to $%04x\n", DBG_PC, data & 0xff, ioaddr);
  return SCPE_OK;
}

t_stat con_read(t_addr ioaddr, uint16 *data) {
  switch (ioaddr & 0x0003) {
  case 0: /* CTRL1 */
    *data = con_ctrl1;
    break;
  case 1:
    *data = con_ctrl2;
    break;
  case 2:
    /* XXX find out whether terminal is attached to console? */
    setbit(con_status, CONS_DSR);
//    else clrbit(con_status, CONS_DSR);
    *data = con_status;
    clrbit(con_status,CONS_DSC); /* acknowledge change in DSR/DCD */
    break;
  case 3:
    *data = con_rcv;
    clrbit(con_status,CONS_DR);
    cpu_assertInt(INT_CONR, FALSE);
  }
  sim_debug(DBG_CON_READ, &con_dev, DBG_PCFORMAT1 "Byte read %02x (pos logic) from $%04x\n", DBG_PC, *data & 0xff, ioaddr);
  
  /* note usart has inverted bus, so returned data must be negated */
  *data = ~(*data);
  return SCPE_OK;
}

/************************************************************************************************
 * Onboard 8253 timer
 ***********************************************************************************************/

struct i8253 {
  uint16 cnt;
  uint16 preset;
  uint16 mode;
  t_bool hilo; /* which half of 16 bit cnt is to be set */
};
struct i8253 tim[3];

IOINFO tim_ioinfo1 = { NULL,         TIM_IOBASE, 4, TIM_TICK_VEC,  6, tim_read, tim_write };
IOINFO tim_ioinfo2 = { &tim_ioinfo1, 0,          0, TIM_INTVL_VEC, 7, tim_read, tim_write };
DEVCTXT tim_ctxt = { &tim_ioinfo2 };

UNIT tim_unit[] = {
  { UDATA (&tim0_svc, 0, 0), CON_POLLRATE, },
  { UDATA (&tim1_svc, 0, 0), CON_POLLRATE, },
  { UDATA (&tim2_svc, 0, 0), CON_POLLRATE, }
};

REG tim_reg[] = {
  { HRDATA (CNT0,  tim[0].cnt, 16) },
  { HRDATA (CNT1,  tim[1].cnt, 16) },
  { HRDATA (CNT2,  tim[2].cnt, 16) },
  { HRDATA (MODE0, tim[0].mode, 8) },
  { HRDATA (MODE1, tim[1].mode, 8) },
  { HRDATA (MODE2, tim[2].mode, 8) },
  { NULL }
};
MTAB tim_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "IOBASE", "IOBASE", NULL, &show_iobase },
  { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR", NULL, &show_iovec },
  { MTAB_XTD|MTAB_VDV, 0, "PRIO",   "PRIO",   NULL, &show_ioprio },
  { 0 }
};
DEBTAB tim_dflags[] = {
  { "WRITE", DBG_TIM_WRITE },
  { "READ",  DBG_TIM_READ },
  { "SVC",   DBG_TIM_SVC },
  { 0, 0 }
};

DEVICE tim_dev = {
  "TIM",    /*name*/
  tim_unit,   /*units*/
  tim_reg,    /*registers*/
  tim_mod,    /*modifiers*/
  3,          /*numunits*/
  16,         /*aradix*/
  16,         /*awidth*/
  1,          /*aincr*/
  8,          /*dradix*/
  8,          /*dwidth*/
  NULL,       /*examine*/
  NULL,       /*deposit*/
  &tim_reset, /*reset*/
  NULL,       /*boot*/
  NULL,       /*attach*/
  NULL,       /*detach*/
  &tim_ctxt,  /*ctxt*/
  DEV_DEBUG,  /*flags*/
  0,          /*dctrl*/
  tim_dflags, /*debflags*/
  NULL,       /*msize*/
  NULL        /*lname*/
};

t_stat tim_reset(DEVICE *dptr) 
{
  DEVCTXT* ctxt = (DEVCTXT*)dptr->ctxt;

  if (dptr->flags & DEV_DIS) {
    del_ioh(ctxt->ioi);
    sim_cancel(&tim_unit[0]);
    sim_cancel(&tim_unit[1]);
    sim_cancel(&tim_unit[2]);
  } else {
    add_ioh(ctxt->ioi);
  }
  return SCPE_OK;
}

t_stat tim_read(t_addr ioaddr, uint16 *data)
{
  int n = ioaddr & 0x0003;
  if (n == 3)
   *data = 0xff;
  else {
    *data = (tim[n].hilo ? tim[n].cnt : (tim[n].cnt >> 8)) & 0xff;
    sim_debug(DBG_TIM_READ, &tim_dev, DBG_PCFORMAT1 "Read %s timer%d: %02x\n", 
      DBG_PC, tim[n].hilo ? "high" : "low", n, *data);
    tim[n].hilo = ! tim[n].hilo;
  }
  return SCPE_OK;
}

static uint16 sethi(uint16 val, uint16 data) {
  val &= 0xff;
  val |= (data << 8);
  return val;
}
static uint16 setlo(uint16 val, uint16 data) {
  val &= 0xff00;
  val |= data;
  return val;
}
t_stat tim_write(t_addr ioaddr, uint16 data)
{
  int n = ioaddr & 0x0003;
  data &= 0xff;
  if (n == 3) {
    n = (data & 0xc0) >> 6;
    sim_debug(DBG_TIM_WRITE, &tim_dev, DBG_PCFORMAT0 "Timer%d: mode=%d\n",
              DBG_PC, n, (data >> 1) & 7);
    if (n == 3) {
      sim_printf("Unimplemented: Mode=0xc0\n");
      return STOP_IMPL;
    }
    if (data & 0x01) {
      sim_printf("Unimplemented: BCD mode: timer=%d\n",n);
      return STOP_IMPL;
    }
    if (!( (data & 0x0e)==0x00 || (data & 0x0e)==0x04)) {
      sim_printf("Unimplemented: Mode not 0 or 2: timer=%d\n",n);
      return STOP_IMPL;
    }
    if ((data & 0x30) != 0x30) {
      sim_printf("Unimplemented: not 16 bit load: timer=%d\n",n);
      return STOP_IMPL;
    }
    tim[n].mode = data;
  } else {
    if (tim[n].hilo) {
      tim[n].preset = sethi(tim[n].preset, data);
      tim[n].cnt = sethi(tim[n].cnt, data);
      if (n < 2) { /* timer 2 is triggered by timer 1 */
        int32 time = 1250000 / tim[n].cnt;
        sim_cancel(&tim_unit[n]);
        sim_activate(&tim_unit[n], time);
      }
    } else {
      tim[n].preset = setlo(tim[n].preset, data);
      tim[n].cnt = setlo(tim[n].cnt, data);
    }
    sim_debug(DBG_TIM_WRITE, &tim_dev, DBG_PCFORMAT0 "Timer%d: %s cnt=%02x\n",
      DBG_PC, n, tim[n].hilo ? "high":"low", data);
    tim[n].hilo = !tim[n].hilo;
  }
  return SCPE_OK;
}

/* baud rate timer 0 is programmed in mode 2 - actually, this is ignored */
static t_stat tim0_svc(UNIT* uptr) 
{
  int32 time = 1250000 / tim[0].preset;
  sim_activate(uptr, time);
  sim_debug(DBG_TIM_SVC, &tim_dev, DBG_PCFORMAT2 "Timer0: SVC call\n", DBG_PC);
  return SCPE_OK;
}

/* system timer 1 is programmed in mode 2, causes interrupt each time it is 0 */
static t_stat tim1_svc(UNIT* uptr) 
{
  int32 time = 1250000 / tim[0].preset;
  sim_debug(DBG_TIM_SVC, &tim_dev, DBG_PCFORMAT2 "Timer1: SVC call\n", DBG_PC);
  sim_activate(uptr, time);
  cpu_raiseInt(INT_TICK);
  reg_ssr |= SSR_TICK; /* notify TICK timer int occurred */
  
  /* handle interval timer */
  if (tim[2].cnt > 0) tim[2].cnt--;
  if (tim[2].cnt == 0) {
    cpu_raiseInt(INT_INTVL);
    reg_ssr |= SSR_INTVL; /* notify INTVL timer int occurred */
    if ((tim[2].mode & 0x0e) == 0x04) {
      tim[2].cnt = tim[2].preset; /* restart timer */
    } /* otherwise single shot */
  }
  return SCPE_OK; 
}

/* interval timer 2 is programmed in mode 0 (single shot) or 2 (rate generator)
 * this is triggered by timer1 - svc is ignored here */
static t_stat tim2_svc(UNIT* uptr)
{
  sim_debug(DBG_TIM_SVC, &tim_dev, DBG_PCFORMAT2 "Timer2: SVC call - should not occur\n", DBG_PC);
  return SCPE_OK;
}

