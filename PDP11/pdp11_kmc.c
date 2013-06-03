/* pdp11_kdp.c: KMC11/DUP11 Emulation
  ------------------------------------------------------------------------------


   Written 2002 by Johnny Eriksson <bygg@stacken.kth.se>

   Adapted to SIMH 3.? by Robert M. A. Jarratt in 2013

  ------------------------------------------------------------------------------

  Modification history:

  14-Apr-13  RJ   Took original sources into latest source code.
  15-Feb-02  JE   Massive changes/cleanups.
  23-Jan-02  JE   Modify for version 2.9.
  17-Jan-02  JE   First attempt.
------------------------------------------------------------------------------*/

/*
** Loose ends, known problems etc:
**
**   We don't handle NXM on the unibus.  At all.  In fact, we don't
**   generate control-outs.
**
**   We don't do anything but full-duplex DDCMP.
**
**   We don't implement buffer flushing.
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

#define KMC_RDX     8

#include "pdp11_dup.h"

#define DF_CMD    0001  /* Print commands. */
#define DF_TX     0002  /* Print tx done. */
#define DF_RX     0004  /* Print rx done. */
#define DF_DATA   0010  /* Print data. */
#define DF_QUEUE  0020  /* Print rx/tx queue changes. */
#define DF_TRC    0040  /* Detailed trace. */
#define DF_INF    0100  /* Info */

extern int32 IREQ (HLVL);
extern int32 tmxr_poll;                                 /* calibrated delay */
extern int32 clk_tps;                                   /* clock ticks per second */
extern int32 tmr_poll;                                  /* instructions per tick */

t_stat unibus_read(int32* data, int32 addr)
{
    t_stat ans;
    uint16 d;

    *data = 0;
    ans = Map_ReadW (addr, 2, &d);
    *data = d;
    return ans;
}

t_stat unibus_write(int32 data, int32 addr)
{
    uint16 d;

    d = data & 0xFFFF;

    return Map_WriteW (addr, 2, &d);
}

t_stat dma_write(int32 ba, uint8* data, int length)
{
  t_stat r;
  uint32 wd;

  if (length <= 0) return SCPE_OK;
  if (ba & 1) {
    r = unibus_read((int32 *)&wd, ba-1);
    if (r != SCPE_OK)
      return r;
    wd &= 0377;
    wd |= (*data++ << 8);
    r = unibus_write(wd, ba-1);
    if (r != SCPE_OK)
      return r;
    length -= 1;
    ba += 1;
  }
  while (length >= 2) {
    wd = *data++;
    wd |= *data++ << 8;
    r = unibus_write(wd, ba);
    if (r != SCPE_OK)
      return r;
    length -= 2;
    ba += 2;
  }
  if (length == 1) {
    r = unibus_read((int32 *)&wd, ba);
    if (r != SCPE_OK)
      return r;
    wd &= 0177400;
    wd |= *data++;
    r = unibus_write(wd, ba);
    if (r != SCPE_OK)
      return r;
  }
  return SCPE_OK;
}

/* dma a block from main memory */

t_stat dma_read(int32 ba, uint8* data, int length)
{
  t_stat r;
  uint32 wd;

  if (ba & 1) {         /* Starting on an odd boundary? */
    r = unibus_read((int32 *)&wd, ba-1);
    if (r != SCPE_OK) {
      return r;
    }
    *data++ = wd >> 8;
    ba += 1;
    length -= 1;
  }
  while (length > 0) {
    r = unibus_read((int32 *)&wd, ba);
    if (r != SCPE_OK) {
      return r;
    }
    *data++ = wd & 0377;
    if (length > 1) {
      *data++ = wd >> 8;
    }
    ba += 2;
    length -= 2;
  }
  return SCPE_OK;
}

/* bits, sel0: */

#define KMC_RUN    0100000  /* Run bit. */
#define KMC_MRC    0040000  /* Master clear. */
#define KMC_CWR    0020000  /* CRAM write. */
#define KMC_SLU    0010000  /* Step Line Unit. */
#define KMC_LUL    0004000  /* Line Unit Loop. */
#define KMC_RMO    0002000  /* ROM output. */
#define KMC_RMI    0001000  /* ROM input. */
#define KMC_SUP    0000400  /* Step microprocessor. */
#define KMC_RQI    0000200  /* Request input. */
#define KMC_IEO    0000020  /* Interrupt enable output. */
#define KMC_IEI    0000001  /* Interrupt enable input. */

/* bits, sel2: */

#define KMC_OVR    0100000  /* Buffer overrun. */
#define KMC_LINE   0177400  /* Line number. */
#define KMC_RDO    0000200  /* Ready for output transaction. */
#define KMC_RDI    0000020  /* Ready for input transaction. */
#define KMC_IOT    0000004  /* I/O type, 1 = rx, 0 = tx. */
#define KMC_CMD    0000003  /* Command code. */
#  define CMD_BUFFIN     0  /*   Buffer in. */
#  define CMD_CTRLIN     1  /*   Control in. */
#  define CMD_BASEIN     3  /*   Base in. */
#  define CMD_BUFFOUT    0  /*   Buffer out. */
#  define CMD_CTRLOUT    1  /*   Control out. */

/* bits, sel6: */

#define BFR_EOM    0010000  /* End of message. */
#define BFR_KIL    0010000  /* Buffer Kill. */

/* buffer descriptor list bits: */

#define BDL_LDS    0100000  /* Last descriptor in list. */
#define BDL_RSY    0010000  /* Resync transmitter. */
#define BDL_XAD    0006000  /* Buffer address bits 17 & 16. */
#define BDL_EOM    0001000  /* End of message. */
#define BDL_SOM    0000400  /* Start of message. */

#define KMC_CRAMSIZE 1024   /* Size of CRAM. */

#ifndef MAXDUP
#  define MAXDUP 2      /* Number of DUP-11's we can handle. */
#endif

#define MAXQUEUE 16     /* Number of rx bdl's we can handle. */

#define MAXMSG 2000     /* Largest message we handle. */

/* local variables: */

int    kmc_running;
uint32 kmc_sel0;
uint32 kmc_sel2;
uint32 kmc_sel4;
uint32 kmc_sel6;
int    kmc_rxi;
int    kmc_txi;

uint16 kmc_microcode[KMC_CRAMSIZE];

struct dupblock {
  int32  dupnumber;         /* Line Number of all DUP11's on Unibus */
  uint32 rxqueue[MAXQUEUE]; /* Queue of bd's to receive into. */
  uint32 rxcount;           /* No. bd's in above. */
  uint32 rxnext;            /* Next bd to receive into. */
  uint32 txqueue[MAXQUEUE]; /* Queue of bd's to transmit. */
  uint32 txcount;           /* No. bd's in above. */
  uint32 txnext;            /* Next bd to transmit. */
  uint32 txnow;             /* No. bd's we are transmitting now. */
  uint8  txbuf[MAXMSG];     /* contains next buffer to transmit */
  uint8  txbuflen;          /* length of message in buffer */
  uint8  txbufbytessent;    /* number of bytes from the message actually sent so far */
};

typedef struct dupblock dupblock;

dupblock dup[MAXDUP] = { 0 };

/* state/timing/etc: */

t_bool kmc_output = FALSE;  /* Flag, need at least one output. */
int32 kmc_output_duetime;   /* time to activate after buffer transmit */

/* forward decls: */

t_stat kmc_rd(int32* data, int32 PA, int32 access);
t_stat kmc_wr(int32 data, int32 PA, int32 access);
int32 kmc_rxint (void);
int32 kmc_txint (void);
void kmc_setrxint();
void kmc_clrrxint();
void kmc_settxint();
void kmc_clrtxint();
t_stat kmc_svc(UNIT * uptr);
t_stat kmc_reset(DEVICE * dptr);

void prbdl(uint32 dbits, DEVICE *dev, int32 ba, int prbuf);

DEBTAB kmc_debug[] = {
    {"CMD",   DF_CMD},
    {"TX",    DF_TX},
    {"RX",    DF_RX},
    {"DATA",  DF_DATA},
    {"QUEUE", DF_QUEUE},
    {"TRC",   DF_TRC},
    {"INF",   DF_INF},
    {0}
};

/* KMC11 data structs: */

#define IOLN_KMC        010

DIB kmc_dib = { IOBA_AUTO, IOLN_KMC, &kmc_rd, &kmc_wr, 2, IVCL (KMCA), VEC_AUTO, {&kmc_rxint, &kmc_txint} };

UNIT kmc_unit = { UDATA (&kmc_svc, 0, 0) };

REG kmc_reg[] = {
  { ORDATA ( SEL0, kmc_sel0, 16) },
  { ORDATA ( SEL2, kmc_sel2, 16) },
  { ORDATA ( SEL4, kmc_sel4, 16) },
  { ORDATA ( SEL6, kmc_sel6, 16) },
  { NULL },
};

MTAB kmc_mod[] = {
  { MTAB_XTD|MTAB_VDV, 010, "address", "ADDRESS",
    &set_addr, &show_addr, NULL, "IP address" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", NULL,
    &set_vec, &show_vec, NULL, "Interrupt vector" },
  { 0 },
};

DEVICE kmc_dev =
{
    "KMC", &kmc_unit, kmc_reg, kmc_mod,
    1, KMC_RDX, 13, 1, KMC_RDX, 8,
    NULL, NULL, &kmc_reset,
    NULL, NULL, NULL, &kmc_dib,
    DEV_UBUS | DEV_DIS | DEV_DISABLE | DEV_DEBUG, 0, kmc_debug
};

void dup_send_complete (int32 dup, int status)
{
    sim_activate_notbefore (&kmc_unit, kmc_output_duetime);
}

t_stat send_buffer(int dupindex)
{
    t_stat r = SCPE_OK;
    dupblock* d;
    d = &dup[dupindex];

    if (d->txnow > 0) {
        if (dup_put_msg_bytes (d->dupnumber, d->txbuf, d->txbuflen, TRUE, TRUE)) {
            int32 speed = dup_get_line_speed(d->dupnumber);

            d->txnext += d->txnow;
            d->txnow = 0;
            kmc_output = TRUE;
            kmc_output_duetime = sim_grtime();
            if (speed > 7)
                kmc_output_duetime += (tmxr_poll * clk_tps)/(speed/8);
            }
        }

    return r;
}

char *format_packet_data(uint8 *data, size_t size)
{
    static char buf[3 * 128 + 1];
    int buflen;
    int i;
    int n;

    buflen = 0;
    n = (size > 128) ? 128 : size;
    for (i = 0; i < n; i++)
    {
        sprintf(&buf[buflen], " %02X", data[i]);
        buflen += 3;
    }

    return buf;
}

/*
** Update interrupt status:
*/

void kmc_updints(void)
{
    if (kmc_sel0 & KMC_IEI) {
        if (kmc_sel2 & KMC_RDI) {
            kmc_setrxint();
        } else {
        kmc_clrrxint();
        }
    }

    if (kmc_sel0 & KMC_IEO) {
        if (kmc_sel2 & KMC_RDO) {
        	kmc_settxint();
		} else {
			kmc_clrtxint();
		}
	}
}

/*
** Try to set the RDO bit.  If it can be set, set it and return true,
** else return false.
*/

t_bool kmc_getrdo(void)
{
  if (kmc_sel2 & KMC_RDO)	/* Already on? */
    return FALSE;
  if (kmc_sel2 & KMC_RDI)   /* Busy doing input? */
    return FALSE;
  kmc_sel2 |= KMC_RDO;
  return TRUE;
}

/*
** Try to do an output command.
*/

void kmc_tryoutput(void)
{
    int i, j;
    dupblock* d;
    uint32 ba;

    if (kmc_output) {
        kmc_output = FALSE;
        for (i = 0; i < MAXDUP; i += 1) {
            d = &dup[i];
            if (d->rxnext > 0) {
                kmc_output = TRUE;  /* At least one, need more scanning. */
                if (kmc_getrdo()) {
                    ba = d->rxqueue[0];
                    kmc_sel2 &= ~KMC_LINE;
                    kmc_sel2 |= (i << 8);
                    kmc_sel2 &= ~KMC_CMD;
                    kmc_sel2 |= CMD_BUFFOUT;
                    kmc_sel2 |= KMC_IOT;	/* Buffer type. */
                    kmc_sel4 = ba & 0177777;
                    kmc_sel6 = (ba >> 2) & 0140000;
                    kmc_sel6 |= BFR_EOM;

                    for (j = 1; j < (int)d->rxcount; j += 1) {
                        d->rxqueue[j-1] = d->rxqueue[j];
                    }
                    d->rxcount -= 1;
                    d->rxnext -= 1;

                    sim_debug(DF_QUEUE, &kmc_dev, "DUP%d: (tryout) ba = %6o, rxcount = %d, rxnext = %d\r\n", i, ba, d->rxcount, d->rxnext);
                    kmc_updints();
                }
                return;
			} 
			if (d->txnext > 0) {
				kmc_output = TRUE;	/* At least one, need more scanning. */
				if (kmc_getrdo()) {
					ba = d->txqueue[0];
					kmc_sel2 &= ~KMC_LINE;
					kmc_sel2 |= (i << 8);
					kmc_sel2 &= ~KMC_CMD;
					kmc_sel2 |= CMD_BUFFOUT;
					kmc_sel2 &= ~KMC_IOT;	/* Buffer type. */
					kmc_sel4 = ba & 0177777;
					kmc_sel6 = (ba >> 2) & 0140000;

					for (j = 1; j < (int)d->txcount; j += 1) {
						d->txqueue[j-1] = d->txqueue[j];
					}
					d->txcount -= 1;
					d->txnext -= 1;

					sim_debug(DF_QUEUE, &kmc_dev, "DUP%d: (tryout) ba = %6o, txcount = %d, txnext = %d\r\n", i, ba, d->txcount, d->txnext);
					kmc_updints();
				}
				return;
			}
		}
	}
}

/*
** Try to start output.  Does nothing if output is already in progress,
** or if there are no packets in the output queue.
*/

void dup_tryxmit(int dupindex)
{
  dupblock* d;

  int pos;			    /* Offset into transmit buffer. */

  uint32 bda;			/* Buffer Descriptor Address. */
  uint32 bd[3];			/* Buffer Descriptor. */
  uint32 bufaddr;		/* Buffer Address. */
  uint32 buflen;		/* Buffer Length. */

  int msglen;			/* Message length. */
  int dcount;			/* Number of descriptors to use. */
  t_bool lds;			/* Found last descriptor. */

  int i;			    /* Random loop var. */

  d = &dup[dupindex];

  if (d->txnow > 0) return;	/* If xmit in progress, quit. */
  if (d->txcount <= d->txnext) return;

  /*
  ** check the transmit buffers we have queued up and find out if
  ** we have a full DDCMP frame.
  */

  lds = FALSE;			/* No last descriptor yet. */
  dcount = msglen = 0;	/* No data yet. */

  /* accumulate length, scan for LDS flag */

  for (i = d->txnext; i < (int)d->txcount; i += 1) {
    bda = d->txqueue[i];
    (void) unibus_read((int32 *)&bd[0], bda);
    (void) unibus_read((int32 *)&bd[1], bda + 2);
    (void) unibus_read((int32 *)&bd[2], bda + 4);

    dcount += 1;		/* Count one more descriptor. */
    msglen += bd[1];		/* Count some more bytes. */
    if (bd[2] & BDL_LDS) {
      lds = TRUE;
      break;
    }
  }
  
  if (!lds) return;		/* If no end of message, give up. */

  d->txnow = dcount;		/* Got a full frame, will send or ignore it. */

  if (msglen <= MAXMSG) {	/* If message fits in buffer, - */
    pos = 0;
    d->txbuflen = msglen;

    for (i = d->txnext; i < (int)(d->txnext + dcount); i += 1) {
      bda = d->txqueue[i];
      (void) unibus_read((int32 *)&bd[0], bda);
      (void) unibus_read((int32 *)&bd[1], bda + 2);
      (void) unibus_read((int32 *)&bd[2], bda + 4);

      bufaddr = bd[0] + ((bd[2] & 06000) << 6);
      buflen = bd[1];

      (void) dma_read(bufaddr, &d->txbuf[pos], buflen);
      pos += buflen;
    }

  send_buffer(dupindex);
  }
}

/*
** Here with a bdl for some new receive buffers.  Set them up.
*/

void dup_newrxbuf(int line, int32 ba)
{
  dupblock* d;
  int32 w3;

  d = &dup[line];

  for (;;) {
    if (d->rxcount < MAXQUEUE) {
      d->rxqueue[d->rxcount] = ba;
      d->rxcount += 1;
      sim_debug(DF_QUEUE, &kmc_dev, "Queued rx buffer %d, descriptor address=0x%04X(%06o octal)\n", d->rxcount - 1, ba, ba);
    }
	else
	{
      sim_debug(DF_QUEUE, &kmc_dev, "(newrxb) no more room for buffers\n");
	}

    (void) unibus_read(&w3, ba + 4);
    if (w3 & BDL_LDS)
      break;

    ba += 6;
  }

  sim_debug(DF_QUEUE, &kmc_dev, "(newrxb) rxcount = %d, rxnext = %d\n", d->rxcount, d->rxnext);

}

/*
** Here with a bdl for some new transmit buffers.  Set them up and then
** try to start output if not already active.
*/

void dup_newtxbuf(int line, int32 ba)
{
  dupblock* d;
  int32 w3;

  d = &dup[line];

  for (;;) {
    if (d->txcount < MAXQUEUE) {
      d->txqueue[d->txcount] = ba;
      d->txcount += 1;
    }
    (void) unibus_read(&w3, ba + 4);
    if (w3 & BDL_LDS)
      break;

    ba += 6;
  }

  sim_debug(DF_QUEUE, &kmc_dev, "DUP%d: (newtxb) txcount = %d, txnext = %d\r\n", line, d->txcount, d->txnext);

  dup_tryxmit(line);		/* Try to start output. */
}

/*
** Here to store a block of data into a receive buffer.
*/

void dup_receive(int line, uint8* data, int count)
{
  dupblock* d;
  uint32 bda;
  uint32 bd[3];
  uint32 ba;
  uint32 bl;

  d = &dup[line];

  if (d->rxcount > d->rxnext) {
    count -= 2;                     /* strip incoming CSR */
    bda = d->rxqueue[d->rxnext];
    (void) unibus_read((int32 *)&bd[0], bda);
    (void) unibus_read((int32 *)&bd[1], bda + 2);
    (void) unibus_read((int32 *)&bd[2], bda + 4);
    sim_debug(DF_QUEUE, &kmc_dev, "dup_receive ba=0x%04x(%06o octal). Descriptor is:\n", bda, bda);
	prbdl(DF_QUEUE, &kmc_dev, bda, 0);

    ba = bd[0] + ((bd[2] & 06000) << 6);
    bl = bd[1];

    if (count > (int)bl) count = bl;	/* XXX */

    sim_debug(DF_QUEUE, &kmc_dev, "Receive buf[%d] writing to address=0x%04X(%06o octal), bytes=%d\n", d->rxnext, ba, ba, count);
    (void) dma_write(ba, data, count);

    bd[2] |= (BDL_SOM | BDL_EOM);

    (void) unibus_write(bd[2], bda + 4);

    d->rxnext += 1;
  }
}

/*
** testing testing
*/

void prbdl(uint32 dbits, DEVICE *dev, int32 ba, int prbuf)
{
  int32 w1, w2, w3;
  int32 dp;

  for (;;) {
    (void) unibus_read(&w1, ba);
    (void) unibus_read(&w2, ba + 2);
    (void) unibus_read(&w3, ba + 4);

    sim_debug(dbits, dev, "  Word 1 = 0x%04X(%06o octal)\n", w1, w1);
    sim_debug(dbits, dev, "  Word 2 = 0x%04X(%06o octal)\n", w2, w2);
    sim_debug(dbits, dev, "  Word 3 = 0x%04X(%06o octal)\n", w3, w3);

    if (prbuf) {
      if (w2 > 20) w2 = 20;
      dp = w1 + ((w3 & 06000) << 6);

      while (w2 > 0) {
		  (void) unibus_read(&w1, dp);
		  dp += 2;
		  w2 -= 2;

		  sim_debug(DF_CMD, dev, " %2x %2x", w1 & 0xff, w1 >> 8);
      }
      sim_debug(DF_CMD, dev, "\r\n");
    }
    if (w3 & BDL_LDS) break;
    ba += 6;
  }
}

void kmc_setrxint()
{
    sim_debug(DF_TRC, &kmc_dev, "set rx interrupt\n");
    kmc_rxi = 1;
    SET_INT(KMCA);
}

void kmc_clrrxint()
{
    sim_debug(DF_TRC, &kmc_dev, "clear rx interrupt\n");
    kmc_rxi = 0;
    CLR_INT(KMCA);
}

void kmc_settxint()
{
    sim_debug(DF_TRC, &kmc_dev, "set tx interrupt\n");
    kmc_txi = 1;
    SET_INT(KMCB);
}

void kmc_clrtxint()
{
    sim_debug(DF_TRC, &kmc_dev, "clear tx interrupt\n");
    kmc_txi = 0;
    CLR_INT(KMCB);
}

/*
** Here to perform an input command:
*/

void kmc_doinput(void)
{
  int line;
  int32 ba;
  dupblock* d;

  line = (kmc_sel2 & 077400) >> 8;
  d = &dup[line];
  ba = ((kmc_sel6 & 0140000) << 2) + kmc_sel4;

  sim_debug(DF_CMD, &kmc_dev, "Input command: sel2=%06o sel4=%06o sel6=%06o\n", kmc_sel2, kmc_sel4, kmc_sel6);
  sim_debug(DF_CMD, &kmc_dev, "Line %d ba=0x%04x(%06o octal)\n", line, ba, ba);
    switch (kmc_sel2 & 7) {
    case 0:
      sim_debug(DF_CMD, &kmc_dev, "Descriptor for tx buffer:\n");
      prbdl(DF_CMD, &kmc_dev, ba, 1);
      break;
    case 4:
      sim_debug(DF_CMD, &kmc_dev, "Descriptor for rx buffer:\n");
      prbdl(DF_CMD, &kmc_dev, ba, 0);
    }

  switch (kmc_sel2 & 7) {
  case 0:			/* Buffer in, data to send: */
    dup_newtxbuf(line, ba);
    break;
  case 1:			/* Control in. */
    /*
    ** This lets us setup the dup for DDCMP mode, and possibly turn up DTR 
    ** since nothing else seems to do that.
    */
    sim_debug(DF_CMD, &kmc_dev, "Running DDCMP in full duplex on Line %d (dup %d):\n", line, d->dupnumber);
    dup_set_DDCMP (d->dupnumber, TRUE);
    dup_set_DTR (d->dupnumber, (kmc_sel6 & 0400) ? TRUE : FALSE));
    dup_set_callback_mode (d->dupnumber, dup_receive, dup_send_complete);
    break;
  case 3:			/* Base in. */
    /*
    ** This tell the KMC what unibus address the dup is at.
    */
    sim_debug(DF_CMD, &kmc_dev, "Setting Line %d DUP unibus address to: 0x%x (0%o octal)\n", line, kmc_sel6+IOPAGEBASE, kmc_sel6+IOPAGEBASE);
    d->dupnumber = dup_csr_to_linenum (kmc_sel6);
    break;
  case 4:			/* Buffer in, receive buffer for us... */
    dup_newrxbuf(line, ba);
    break;
  }
}

/*
** master clear the KMC:
*/

void kmc_mclear(void)
{
  int i;
  dupblock* d;

  sim_debug(DF_INF, &kmc_dev, "Master clear\n");
  kmc_running = 0;
  kmc_sel0 = KMC_MRC;
  kmc_sel2 = 0;
  kmc_sel4 = 0;
  kmc_sel6 = 0;
  kmc_rxi = 0;
  kmc_txi = 0;

  /* clear out the dup's as well. */

  for (i = 0; i < MAXDUP; i += 1) {
    d = &dup[i];
    d->rxcount = 0;
    d->rxnext = 0;
    d->txcount = 0;
    d->txnext = 0;
    d->txnow = 0;
  }
  sim_cancel(&kmc_unit);	/* Stop the clock. */
  sim_activate_after(&kmc_unit, 2000000);
}

/*
** KMC11, read registers:
*/

t_stat kmc_rd(int32* data, int32 PA, int32 access)
{
  switch ((PA >> 1) & 03) {
  case 00:
    *data = kmc_sel0;
    break;
  case 01:
    *data = kmc_sel2;
    break;
  case 02:
    *data = kmc_sel4;
    break;
  case 03:
	if (kmc_sel0 == KMC_RMO)
	{
		kmc_sel6 = kmc_microcode[kmc_sel4 & (KMC_CRAMSIZE - 1)];
	}
    *data = kmc_sel6;
    break;
  }
 
  sim_debug(DF_TRC, &kmc_dev, "kmc_rd(), addr=0%o access=%d, result=0x%04x\n", PA, access, *data);
  return SCPE_OK;
}

void kmc_domicroinstruction()
{
	static uint32 save;
	if (kmc_sel6 == 041222) /* MOVE <MEM><BSEL2> */
	{
		kmc_sel2 = (kmc_sel2 & ~0xFF) |  (save & 0xFF);
	}
	else if (kmc_sel6 == 0122440) /* MOVE <BSEL2><MEM> */
	{
		save = kmc_sel2 & 0xFF;
	}
}

/*
** KMC11, write registers:
*/

t_stat kmc_wr(int32 data, int32 PA, int32 access)
{
  uint32 toggle;
  int reg = PA & 07;
  int sel = (PA >> 1) & 03;

  if (access == WRITE)
  {
      sim_debug(DF_TRC, &kmc_dev, "kmc_wr(), addr=0%08o, SEL%d, data=0x%04x\n", PA, reg, data);
  }
  else
  {
      sim_debug(DF_TRC, &kmc_dev, "kmc_wr(), addr=0x%08o, BSEL%d, data=%04x\n", PA, reg, data);
  }

  switch (sel) {
  case 00:
    if (access == WRITEB) {
      data = (PA & 1)
	? (((data & 0377) << 8) | (kmc_sel0 & 0377))
	: ((data & 0377) | (kmc_sel0 & 0177400));
    }
    toggle = kmc_sel0 ^ data;
    kmc_sel0 = data;
    if (kmc_sel0 & KMC_MRC) {
      kmc_mclear();
      break;
    }
    if ((toggle & KMC_CWR) && (toggle & KMC_RMO) && !(data & KMC_CWR) && !(data & KMC_RMO)) {
      kmc_microcode[kmc_sel4 & (KMC_CRAMSIZE - 1)] = kmc_sel6;
    }

	if ((toggle & KMC_RMI) && (toggle & KMC_SUP) && !(data & KMC_RMI) && !(data & KMC_SUP))
	{
		kmc_domicroinstruction();
	}

    if (toggle & KMC_RUN) {	/* Changing the run bit? */
      if (kmc_sel0 & KMC_RUN)
	  {
        sim_debug(DF_INF, &kmc_dev, "Started RUNing\n");
		kmc_running = 1;
      }
	  else
	  {
        sim_debug(DF_INF, &kmc_dev, "Stopped RUNing\n");
	    sim_cancel(&kmc_unit);
		kmc_running = 0;
      }
    }
    break;
  case 01:
    if (access == WRITEB) {
      data = (PA & 1)
	? (((data & 0377) << 8) | (kmc_sel2 & 0377))
	: ((data & 0377) | (kmc_sel2 & 0177400));
    }
	if (kmc_running)
	{
		if ((kmc_sel2 & KMC_RDI) && (!(data & KMC_RDI))) {
			kmc_sel2 = data;
			kmc_doinput();
		} else if ((kmc_sel2 & KMC_RDO) && (!(data & KMC_RDO))) {
			kmc_sel2 = data;
			kmc_tryoutput();
		} else {
			kmc_sel2 = data;
		}
	}
	else
	{
		kmc_sel2 = data;
	}
    break;
  case 02:
    if (kmc_sel0 & KMC_RMO) {
      kmc_sel6 = kmc_microcode[data & (KMC_CRAMSIZE - 1)];
    }
    kmc_sel4 = data;
    break;
  case 03:
    kmc_sel6 = data;
    break;
  }

  if (kmc_running)
  {
	  if (kmc_output) {
		  kmc_tryoutput();
	  }
	  if (kmc_sel0 & KMC_RQI) {
		  if (!(kmc_sel2 & KMC_RDO)) {
			  kmc_sel2 |= KMC_RDI;
		  }
	  }

	  kmc_updints();
  }

  return SCPE_OK;
}

int32 kmc_rxint (void)
{
	int32 ans = 0; /* no interrupt request active */
	if (kmc_rxi != 0)
	{
		ans = kmc_dib.vec;
		kmc_clrrxint();
	}

	sim_debug(DF_TRC, &kmc_dev, "rx interrupt ack %d\n", ans);

	return ans;
}

int32 kmc_txint (void)
{
	int32 ans = 0; /* no interrupt request active */
	if (kmc_txi != 0)
	{
		ans = kmc_dib.vec + 4;
		kmc_clrtxint();
	}

	sim_debug(DF_TRC, &kmc_dev, "tx interrupt ack %d\n", ans);

	return ans;
}

/*
** KMC11 service routine:
*/

t_stat kmc_svc (UNIT* uptr)
{
    int dupno;

    dupno = uptr->u3;

    if (kmc_output) {
        kmc_tryoutput();		/* Try to do an output transaction. */
    }
    sim_activate_after(uptr, 2000000);
    return SCPE_OK;
}

/*
** KMC11, reset device:
*/

t_stat kmc_reset(DEVICE* dptr)
{
	kmc_sel0 = 0;
	kmc_sel2 = 0;
	kmc_sel4 = 0;
	kmc_sel6 = 0;
    return auto_config (dptr->name, ((dptr->flags & DEV_DIS)? 0: 1 ));  /* auto config */
}
