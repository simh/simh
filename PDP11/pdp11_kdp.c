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
**   We don't really implement the DUP registers.
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
#define DUP_RDX     8

extern int32 IREQ (HLVL);
extern int32 tmxr_poll;                                 /* calibrated delay */
extern int32 clk_tps;                                   /* clock ticks per second */
extern int32 tmr_poll;                                  /* instructions per tick */

#include "sim_tmxr.h"

#define DF_CMD    0001	/* Print commands. */
#define DF_TX     0002	/* Print tx done. */
#define DF_RX     0004	/* Print rx done. */
#define DF_DATA   0010	/* Print data. */
#define DF_QUEUE  0020	/* Print rx/tx queue changes. */
#define DF_TRC    0040	/* Detailed trace. */
#define DF_INF    0100  /* Info */

//t_stat sync_open(int* retval, char* cptr)
//{
//	return SCPE_OK;
//}
int sync_read(int line, uint8* packet, int length)
{
	return 0;
}

//void sync_write(int line, uint8* packet, int length)
//{
//}

//void sync_close(int line)
//{
//}

t_stat unibus_read(int32* data, int32 addr)
{
	t_stat ans;
	uint16 d;

	*data = 0;
	ans = Map_ReadW (addr, 2, &d);
	*data = d;
	return ans;
}

extern t_stat unibus_write(int32 data, int32 addr)
{
    uint16 d;

    d = data & 0xFFFF;

//printf("dma ub write 0x%08x=%06ho(oct)\n", addr, d);
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

  if (ba & 1) {		/* Starting on an odd boundary? */
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

#define KMC_RUN    0100000	/* Run bit. */
#define KMC_MRC    0040000	/* Master clear. */
#define KMC_CWR    0020000	/* CRAM write. */
#define KMC_SLU    0010000	/* Step Line Unit. */
#define KMC_LUL    0004000	/* Line Unit Loop. */
#define KMC_RMO    0002000	/* ROM output. */
#define KMC_RMI    0001000	/* ROM input. */
#define KMC_SUP    0000400	/* Step microprocessor. */
#define KMC_RQI    0000200	/* Request input. */
#define KMC_IEO    0000020	/* Interrupt enable output. */
#define KMC_IEI    0000001	/* Interrupt enable input. */

/* bits, sel2: */

#define KMC_OVR    0100000	/* Buffer overrun. */
#define KMC_LINE   0177400	/* Line number. */
#define KMC_RDO    0000200	/* Ready for output transaction. */
#define KMC_RDI    0000020	/* Ready for input transaction. */
#define KMC_IOT    0000004	/* I/O type, 1 = rx, 0 = tx. */
#define KMC_CMD    0000003	/* Command code. */
#  define CMD_BUFFIN     0	/*   Buffer in. */
#  define CMD_CTRLIN     1	/*   Control in. */
#  define CMD_BASEIN     3	/*   Base in. */
#  define CMD_BUFFOUT    0	/*   Buffer out. */
#  define CMD_CTRLOUT    1	/*   Control out. */

/* bits, sel6: */

#define BFR_EOM    0010000	/* End of message. */
#define BFR_KIL    0010000	/* Buffer Kill. */

/* buffer descriptor list bits: */

#define BDL_LDS    0100000	/* Last descriptor in list. */
#define BDL_RSY    0010000	/* Resync transmitter. */
#define BDL_XAD    0006000	/* Buffer address bits 17 & 16. */
#define BDL_EOM    0001000	/* End of message. */
#define BDL_SOM    0000400	/* Start of message. */

#define KMC_CRAMSIZE 1024	/* Size of CRAM. */

#ifndef MAXDUP
#  define MAXDUP 2		/* Number of DUP-11's we can handle. */
#endif

#define MAXQUEUE 16		/* Number of rx bdl's we can handle. */

#define MAXMSG 2000		/* Largest message we handle. */

/* local variables: */

int    kmc_running;
uint32 kmc_sel0;
uint32 kmc_sel2;
uint32 kmc_sel4;
uint32 kmc_sel6;
int    kmc_rxi;
int    kmc_txi;

uint16 kmc_microcode[KMC_CRAMSIZE];

uint32 dup_rxcsr[MAXDUP];
uint32 dup_rxdbuf[MAXDUP];
uint32 dup_parcsr[MAXDUP];
uint32 dup_txcsr[MAXDUP];
uint32 dup_txdbuf[MAXDUP];

struct dupblock {
  uint32 rxqueue[MAXQUEUE];	/* Queue of bd's to receive into. */
  uint32 rxcount;		/* No. bd's in above. */
  uint32 rxnext;		/* Next bd to receive into. */
  uint32 txqueue[MAXQUEUE];	/* Queue of bd's to transmit. */
  uint32 txcount;		/* No. bd's in above. */
  uint32 txnext;		/* Next bd to transmit. */
  uint32 txnow;			/* No. bd's we are transmitting now. */
  uint8  txbuf[MAXMSG + 2]; /* contains next buffer to transmit, including two bytes for length */
  uint8  txbuflen;      /* length of message in buffer */
  uint8  txbufbytessent;/* number of bytes from the message actually sent so far */
};

typedef struct dupblock dupblock;

dupblock dup[MAXDUP] = { 0 };

/* state/timing/etc: */

t_bool kmc_output = FALSE;	/* Flag, need at least one output. */
int kmc_interval = 10000;	/* Polling interval. */

TMLN kdp_ldsc[MAXDUP];                   /* line descriptors */
TMXR kdp_desc[MAXDUP] = 
{
	{ 1, 0, 0, &kdp_ldsc[0] },
	{ 1, 0, 0, &kdp_ldsc[1] }
};

extern int32 tmxr_poll;                                 /* calibrated delay */

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

t_stat dup_rd(int32* data, int32 PA, int32 access);
t_stat dup_wr(int32 data, int32 PA, int32 access);
t_stat dup_svc(UNIT * uptr);
t_stat dup_reset(DEVICE * dptr);
t_stat dup_attach(UNIT * uptr, char * cptr);
t_stat dup_detach(UNIT * uptr);
void prbdl(uint32 dbits, DEVICE *dev, int32 ba, int prbuf);

t_stat send_packet(DEVICE *device, TMLN *lp, uint8 *buf, size_t size);

DEBTAB kmc_debug[] = {
    {"CMD",   DF_CMD},
    {"TX",    DF_TX},
    {"RX",    DF_RX},
    {"DATA",  DF_DATA},
    {"QUEUE", DF_QUEUE},
    {"TRC",   DF_TRC},
	{"INF",   DF_INF},
	{ "TMXRXMT", TMXR_DBG_XMT },
	{ "TMXRRCV", TMXR_DBG_RCV },
	{ "TMXRASY", TMXR_DBG_ASY },
	{ "TMXRTRC", TMXR_DBG_TRC },
	{ "TMXRCON", TMXR_DBG_CON },
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

  { ORDATA ( DEBUG, kmc_debug, 32) },
  { DRDATA ( INTERVAL, kmc_interval, 32) },

  { GRDATA (DEVADDR, kmc_dib.ba, KMC_RDX, 32, 0), REG_HRO },
/*  { FLDATA (*DEVENB, kmc_dib.enb, 0), REG_HRO }, */
  { NULL },
};

MTAB kmc_mod[] = {
  { MTAB_XTD|MTAB_VDV, 010, "address", "ADDRESS",
    &set_addr, &show_addr, NULL, "IP address" },
/*  { MTAB_XTD|MTAB_VDV, 1, NULL, "ENABLED",
    &set_enbdis, NULL, &kmc_dib },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLED",
    &set_enbdis, NULL, &kmc_dib },*/
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
	DEV_UBUS | DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, kmc_debug
};

/* DUP11 data structs: */

#define IOLN_DUP        010

DIB dup0_dib = { IOBA_AUTO, IOLN_DUP, &dup_rd, &dup_wr, 0 };
DIB dup1_dib = { IOBA_AUTO, IOLN_DUP, &dup_rd, &dup_wr, 0 };

UNIT dup_unit[MAXDUP] = {
  { UDATA (&dup_svc, UNIT_ATTABLE, 0) },
  { UDATA (&dup_svc, UNIT_ATTABLE, 0) }
};

REG dup0_reg[] =
{
  { GRDATA (DEVADDR, dup0_dib.ba, DUP_RDX, 32, 0), REG_HRO },
/*  { FLDATA (*DEVENB, dup0_dib.enb, 0), REG_HRO },*/
  { NULL },
};

REG dup1_reg[] =
{
  { GRDATA (DEVADDR, dup1_dib.ba, DUP_RDX, 32, 0), REG_HRO },
/*  { FLDATA (*DEVENB, dup1_dib.enb, 0), REG_HRO },*/
  { NULL },
};

MTAB dup_mod[] = {
  { MTAB_XTD|MTAB_VDV, 010, "address", "ADDRESS",
    &set_addr, &show_addr },
/*  { MTAB_XTD|MTAB_VDV, 1, NULL, "ENABLED",
    &set_enbdis, NULL, &dup_dib },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "DISABLED",
    &set_enbdis, NULL, &dup_dib },*/
  { 0 },
};

DEVICE dup_dev[] =
{
	{
		"DUP0", &dup_unit[0], dup0_reg, dup_mod,
		1, DUP_RDX, 13, 1, DUP_RDX, 8,
		NULL, NULL, &dup_reset,
		NULL, &dup_attach, &dup_detach, &dup0_dib,
		DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG, 0, kmc_debug
	},
	{
		"DUP1", &dup_unit[1], dup1_reg, dup_mod,
		1, DUP_RDX, 13, 1, DUP_RDX, 8,
		NULL, NULL, &dup_reset,
		NULL, &dup_attach, &dup_detach, &dup1_dib,
		DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG, 0, kmc_debug
	}
};

t_stat send_buffer(int dupindex)
{
	t_stat r = SCPE_OK;
    dupblock* d;

	d = &dup[dupindex];

	if (d->txnow > 0 && kdp_ldsc[dupindex].conn)
	{
		r = send_packet(&dup_dev[dupindex], &kdp_ldsc[dupindex], d->txbuf, d->txbuflen);
		d->txnext += d->txnow;
		kmc_output = TRUE;
	}

	d->txnow = 0;

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

t_stat send_packet(DEVICE *device, TMLN *lp, uint8 *buf, size_t size)
{
	int bytesLeft;
	size_t i;
	t_stat r = SCPE_OK;

	sim_debug(DF_DATA, device, "Sending packet, length %d:%s\n", size - 2, format_packet_data(buf + 2, size - 2));

	for (i=0; i<size; i++) {
		r = tmxr_putc_ln (lp, buf[i]);
	    if (r != SCPE_OK) sim_debug(DF_DATA, device, "Failed to put a data byte\n");
	}
	bytesLeft = tmxr_send_buffered_data(lp);
	if (bytesLeft != 0) sim_debug(DF_DATA, device, "Bytes left after send %d\n", bytesLeft);

	return r;
}

int read_packet(DEVICE *device, TMLN *lp, uint8 *buf, size_t size)
{
	size_t i;
	size_t actualLength = 0;
	int32 firstByte;

	tmxr_poll_rx(lp->mp);

	firstByte = tmxr_getc_ln(lp);
	if (firstByte & TMXR_VALID)
	{
		actualLength = (firstByte & 0xFF) << 8;
		actualLength += (tmxr_getc_ln(lp) & 0xFF);

		if (actualLength > size)
		{
			sim_debug(DF_INF, device, "Received message too long, expected %d, but was %d\n", size, actualLength);
			actualLength = 0;
		}
		else
		{
			for (i = 0; i < actualLength; i++)
			{
				buf[i] = (uint8)(tmxr_getc_ln(lp) & 0xFF);
			}

			sim_debug(DF_DATA, device, "Read packet, length %d:%s\n", actualLength, format_packet_data(buf, actualLength));
		}
	}

	return actualLength;
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
  if (kmc_sel2 & KMC_RDI)	/* Busy doing input? */
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
				kmc_output = TRUE;	/* At least one, need more scanning. */
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

					sim_debug(DF_QUEUE, &dup_dev[i], "DUP%d: (tryout) ba = %6o, rxcount = %d, rxnext = %d\r\n", i, ba, d->rxcount, d->rxnext);
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

					sim_debug(DF_QUEUE, &dup_dev[i], "DUP%d: (tryout) ba = %6o, txcount = %d, txnext = %d\r\n", i, ba, d->txcount, d->txnext);
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

  int pos;			/* Offset into transmit buffer. */

  uint32 bda;			/* Buffer Descriptor Address. */
  uint32 bd[3];			/* Buffer Descriptor. */
  uint32 bufaddr;		/* Buffer Address. */
  uint32 buflen;		/* Buffer Length. */

  int msglen;			/* Message length. */
  int dcount;			/* Number of descriptors to use. */
  t_bool lds;			/* Found last descriptor. */

  int i;			/* Random loop var. */
  int delay;			/* Estimated transmit time. */

  extern int32 tmxr_poll;	/* calibrated delay */

  d = &dup[dupindex];

  if (d->txnow > 0) return;	/* If xmit in progress, quit. */
  if (d->txcount <= d->txnext) return;

  /*
  ** check the transmit buffers we have queued up and find out if
  ** we have a full DDCMP frame.
  */

  lds = FALSE;			/* No last descriptor yet. */
  dcount = msglen = 0;		/* No data yet. */

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
	d->txbuf[0] = (uint8)(msglen>>8) & 0xFF;
	d->txbuf[1] = (uint8)msglen & 0xFF;
	d->txbuflen = msglen + 2;
    pos = 2;

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

#define IPS() (tmxr_poll * 50)	/* UGH! */

  /*
  ** Delay calculation:
  ** delay (instructions) = bytes * IPS * 8 / speed;
  ** either do this in floating point, or be very careful about
  ** overflows...
  */

  delay = IPS() / (19200 >> 10);
  delay *= msglen;
  delay >>= 7;

  //sim_activate(&dup_unit[dupindex], delay);
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
      sim_debug(DF_QUEUE, &dup_dev[line], "Queued rx buffer %d, descriptor address=0x%04X(%06o octal)\n", d->rxcount - 1, ba, ba);
    }
	else
	{
      sim_debug(DF_QUEUE, &dup_dev[line], "(newrxb) no more room for buffers\n");
	}

    (void) unibus_read(&w3, ba + 4);
    if (w3 & BDL_LDS)
      break;

    ba += 6;
  }

  sim_debug(DF_QUEUE, &dup_dev[line], "(newrxb) rxcount = %d, rxnext = %d\n", d->rxcount, d->rxnext);

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

  sim_debug(DF_QUEUE, &dup_dev[line], "DUP%d: (newtxb) txcount = %d, txnext = %d\r\n", line, d->txcount, d->txnext);

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
    bda = d->rxqueue[d->rxnext];
    (void) unibus_read((int32 *)&bd[0], bda);
    (void) unibus_read((int32 *)&bd[1], bda + 2);
    (void) unibus_read((int32 *)&bd[2], bda + 4);
    sim_debug(DF_QUEUE, &dup_dev[line], "dup_receive ba=0x%04x(%06o octal). Descriptor is:\n", bda, bda);
	prbdl(DF_QUEUE, &dup_dev[line], bda, 0);

    ba = bd[0] + ((bd[2] & 06000) << 6);
    bl = bd[1];

    if (count > (int)bl) count = bl;	/* XXX */

    sim_debug(DF_QUEUE, &dup_dev[line], "Receive buf[%d] writing to address=0x%04X(%06o octal), bytes=%d\n", d->rxnext, ba, ba, count);
    (void) dma_write(ba, data, count);

    bd[2] |= (BDL_SOM | BDL_EOM);

    (void) unibus_write(bd[2], bda + 4);

    d->rxnext += 1;
  }
}

/*
** Try to receive data for a given line:
*/

void dup_tryreceive(int dupindex)
{
  int length;
  uint8 buffer[MAXMSG];

  if (kdp_ldsc[dupindex].conn) {	/* Got a sync line? */
    length = read_packet(&dup_dev[dupindex], &kdp_ldsc[dupindex], buffer, MAXMSG);
    if (length > 0) {		/* Got data? */
      sim_debug(DF_RX, &dup_dev[dupindex], "DUP%d: receiving %d bytes\r\n", dupindex, length);
      dup_receive(dupindex, buffer, length);
      kmc_output = TRUE;	/* Flag this. */
    }
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

  line = (kmc_sel2 & 077400) >> 8;
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
    ** The only thing this does is tell us to run DDCMP, in full duplex,
    ** but that is the only thing we know how to do anyway...
    */
    break;
  case 3:			/* Base in. */
    /*
    ** The only thing this does is tell the KMC what unibus address
    ** the dup is at.  But we already know...
    */
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
    sim_cancel(&dup_unit[i]);	/* Stop xmit wait. */
    sim_clock_coschedule(&dup_unit[i], tmxr_poll);
  }
  sim_cancel(&kmc_unit);	/* Stop the clock. */
  sim_clock_coschedule(&kmc_unit, tmxr_poll);
}

/*
** DUP11, read registers:
*/

t_stat dup_rd(int32* data, int32 PA, int32 access)
{
  int dupno;

  dupno = ((PA - dup0_dib.ba) >> 3) & (MAXDUP - 1);

  switch ((PA >> 1) & 03) {
  case 00:
    *data = dup_rxcsr[dupno];
    break;
  case 01:
    *data = dup_rxdbuf[dupno];
    break;
  case 02:
    *data = dup_txcsr[dupno];
    break;
  case 03:
    *data = dup_txdbuf[dupno];
    break;
  }
  return SCPE_OK;
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

/*
** DUP11, write registers:
*/

t_stat dup_wr(int32 data, int32 PA, int32 access)
{
  int dupno;

  dupno = ((PA - dup0_dib.ba) >> 3) & (MAXDUP - 1);

  switch ((PA >> 1) & 03) {
  case 00:
    dup_rxcsr[dupno] = data;
    break;
  case 01:
    dup_parcsr[dupno] = data;
    break;
  case 02:
    dup_txcsr[dupno] = data;
    break;
  case 03:
    dup_txdbuf[dupno] = data;
    break;
  }
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
** DUP11 service routine:
*/

t_stat dup_svc(UNIT* uptr)
{
  int dupindex;
  int32 ln;
  dupblock* d;

  dupindex = uptr->u3;
  d = &dup[dupindex];
  //printf("dup_svc %d\n", dupindex);

  ln = tmxr_poll_conn(&kdp_desc[dupindex]);
  if (ln >= 0)
  {
	  kdp_ldsc[dupindex].rcve = 1;
  }

  tmxr_poll_rx(&kdp_desc[dupindex]);
  tmxr_poll_tx(&kdp_desc[dupindex]);

  //send_buffer(dupindex);
  //if (d->txnow > 0) {
  //  d->txnext += d->txnow;
  //  d->txnow = 0;
  //  kmc_output = TRUE;
  //}

  if (d->txcount > d->txnext) {
    dup_tryxmit(dupindex);
  }

  sim_clock_coschedule (uptr, tmxr_poll);

  return SCPE_OK;
}

/*
** KMC11 service routine:
*/

t_stat kmc_svc (UNIT* uptr)
{
  int i;
  int dupno;

  dupno = uptr->u3;

  for (i = 0; i < MAXDUP; i += 1) {
    dup_tryreceive(i);
  }
  if (kmc_output) {
    kmc_tryoutput();		/* Try to do an output transaction. */
  }  
  sim_clock_coschedule (uptr, tmxr_poll);
  //sim_activate(&kmc_unit, kmc_interval);
  return SCPE_OK;
}

/*
** DUP11, reset device:
*/

t_stat dup_reset(DEVICE* dptr)
{
//  static t_bool firsttime = TRUE;
  int i;

  for (i = 0; i < MAXDUP; i++)
  {
	  dup_unit[i].u3 = i;
	  kdp_ldsc[i].rcve = 1;
  }

  //if (firsttime) {
  //  for (i = 1; i < MAXDUP; i += 1) {
  //    dup_unit[i] = dup_unit[0]; /* Copy all the units. */
  //  }
  //  for (i = 0; i < MAXDUP; i += 1) {
  //    //tmxr_reset_ln(&kdp_ldsc[i]);
  //    dup_unit[i].u3 = i;	/* Link dupblock to unit. */
  //  }
  //  firsttime = FALSE;		/* Once-only init done now. */
  //}

  return auto_config (dptr->name, (dptr->flags & DEV_DIS)? 0: 1 );    /* auto config */
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

    return auto_config (dptr->name, (dptr->flags & DEV_DIS)? 0: 1 );    /* auto config */
}

/*
** DUP11, attach device:
*/

t_stat dup_attach(UNIT* uptr, char* cptr)
{
  int dupno;
  t_stat r;
  char* tptr;

  dupno = uptr->u3;

  tptr = (char *)malloc(strlen(cptr) + 1);
  if (tptr == NULL) return SCPE_MEM;
  strcpy(tptr, cptr);

  r = tmxr_attach(&kdp_desc[dupno], uptr, cptr);
  if (r != SCPE_OK) {
    free(tptr);
    return r;
  }

  uptr->filename = tptr;
  uptr->flags |= UNIT_ATT;

  return SCPE_OK;
}

/*
** DUP11, detach device:
*/

t_stat dup_detach(UNIT* uptr)
{
  int dupno;

  dupno = uptr->u3;

  tmxr_detach(&kdp_desc[dupno], uptr);

  if (uptr->flags & UNIT_ATT) {
    free(uptr->filename);
    uptr->filename = NULL;
    uptr->flags &= ~UNIT_ATT;
  }

  return SCPE_OK;
}
