/* h316_hi.c- BBN ARPAnet IMP Host Interface
   Based on the SIMH simulator package written by Robert M Supnik.

   Copyright (c) 2013 Robert Armstrong, bob@jfcl.com.

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
   ROBERT ARMSTRONG BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert Armstrong shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert Armstrong.

   hi           host interface

   21-May-13    RLA     New file

   The host interface is one of the BBN engineered devices unique to the
   ARPAnet IMP.  This is the famous "1822" card which connected each IMP to a
   host computer - a DECSYSTEM-10, an SDS Sigma 7, an IBM 360/90, a CDC6600,
   or any one of many other ARPAnet hosts.  The idea is to simulate this 
   interface by using a TCP/UDP connection to another simh instance emulating
   the host machine and running the ARPAnet host software.

   Presently the details of the host interface card are not well known, and
   this implementation is simply a place holder.  It's enough to allow the IMP
   software to run, but not actually to communicate with a host.  The IMP simply
   believes that all the attached hosts are down at the moment.

   Host interface state is maintained in a set of position and state variables:

   Host state is maintained in the following variables -

        TBA                     TBA

   TODO

   IMPLEMENT THIS MODULE!!!
*/

// 1822 sect 4.5 Host cable connections
//
//   IMP Ready
//
//      IMP  Ready Test         Host --> IMP   trigger IMP Master Ready
//      IMP  Master Ready       IMP  --> Host
//
//   Host Ready
//
//      Host Ready Test         IMP  --> Host  trigger Host Master Ready
//      Host Master Ready       IMP  --> Host
//
//   Host to IMP Data
//
//      Host-to-IMP Data Line   Host --> IMP
//      There's-Your-Host-Bit   Host --> IMP
//      Ready-For-Next-Host-Bit IMP  --> Host
//      Last-Host-Bit           Host --> IMP
//
//   IMP to Host Data
//
//      IMP-to-Host Data Line   IMP  --> Host
//      There's-Your-IMP-Bit    IMP  --> Host
//      Ready-For-Next-IMP-Bit  Host --> IMP
//      Last-IMP-Bit            IMP  --> Host
//

// Last-IMP-Bit is implemented as an out-of-band flag in UDP_PACKET
#define PFLG_FINAL 00001

// TODO
//
// For the nonce, assume ready bits are always on. We need an out-of-band
// packet exchange to model the ready bit behavior. (This could also reset
// the UDP_PACKET sequence numbers.)


#ifdef VM_IMPTIP
#include "h316_defs.h"          // H316 emulator definitions
#include "h316_imp.h"           // ARPAnet IMP/TIP definitions

// Externals from other parts of simh ...
extern uint16 dev_ext_int, dev_ext_enb; // current IRQ and IEN bit vectors
extern int32 PC;                        // current PC (for debug messages)
extern int32 stop_inst;                 // needed by IOBADFNC()
extern uint16 M[];                      // main memory (for DMC access)

// Forward declarations ...
int32  hi_io (uint16 line, int32 inst, int32 fnc, int32 dat, int32 dev);
int32 hi1_io (int32 inst, int32 fnc, int32 dat, int32 dev);
int32 hi2_io (int32 inst, int32 fnc, int32 dat, int32 dev);
int32 hi3_io (int32 inst, int32 fnc, int32 dat, int32 dev);
int32 hi4_io (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat hi_rx_service (UNIT *uptr);
void hi_rx_local (uint16 line, uint16 txnext, uint16 txcount);
t_stat hi_reset (DEVICE *dptr);
t_stat hi_attach (UNIT *uptr, CONST char *cptr);
t_stat hi_detach (UNIT *uptr);



////////////////////////////////////////////////////////////////////////////////
//////////////////////   D A T A    S T R U C T U R E S   //////////////////////
////////////////////////////////////////////////////////////////////////////////

// Host interface data blocks ...
//   The HIDB is our own internal data structure for each host.  It keeps data
// about the TCP/IP connection, buffers, etc.
#define HI_HIDB(N)  {FALSE, FALSE, 0, 0, 0, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, 0, HI_TXBPS}
HIDB hi1_db = HI_HIDB(1), hi2_db = HI_HIDB(2);
HIDB hi3_db = HI_HIDB(3), hi4_db = HI_HIDB(4);

// Host Device Information Blocks ...
//   The DIB is the structure simh uses to keep track of the device IO address
// and IO service routine.  It can also hold the DMC channel, but we don't use
// that because it's unit specific.
#define HI_DIB(N) {HI##N, 1, HI##N##_RX_DMC, HI##N##_TX_DMC, \
                   INT_V_HI##N##RX, INT_V_HI##N##TX, &hi##N##_io, N}
DIB hi1_dib = HI_DIB(1), hi2_dib = HI_DIB(2);
DIB hi3_dib = HI_DIB(3), hi4_dib = HI_DIB(4);

// Host Device Unit data ...
//   simh uses the unit data block primarily to schedule device service events.
// The UNIT data also contains four "user" fields which devices can reuse for
// any purpose and we take advantage of that to store the line number.
#define hline  u3    // our host line number is stored in user data 3
#define HI_UNIT(N) {UDATA (&hi_rx_service, UNIT_ATTABLE, 0), HI_POLL_DELAY, N, 0, 0, 0}
UNIT hi1_unit = HI_UNIT(1), hi2_unit = HI_UNIT(2);
UNIT hi3_unit = HI_UNIT(3), hi4_unit = HI_UNIT(4);

// Host Device Registers ...
//   These are the simh device "registers" - they c can be viewed with the
// "EXAMINE HIxn STATE" command and modified by "DEPOSIT HIxn ..."
#define HI_REG(N) {                                                     \
  { DRDATA (POLL,  hi##N##_unit.wait,  24), REG_NZ + PV_LEFT },          \
  { FLDATA (RXIRQ, dev_ext_int, INT_V_HI##N##RX-INT_V_EXTD) },          \
  { FLDATA (RXIEN, dev_ext_enb, INT_V_HI##N##RX-INT_V_EXTD) },          \
  { DRDATA (RXTOT, hi##N##_db.rxtotal,32), REG_RO + PV_LEFT },          \
  { FLDATA (TXIRQ, dev_ext_int, INT_V_HI##N##TX-INT_V_EXTD) },          \
  { FLDATA (TXIEN, dev_ext_enb, INT_V_HI##N##TX-INT_V_EXTD) },          \
  { DRDATA (TXTOT, hi##N##_db.txtotal,32), REG_RO + PV_LEFT },          \
  { FLDATA (LLOOP, hi##N##_db.iloop,   0),          PV_RZRO },          \
  { FLDATA (ERROR, hi##N##_db.error,   0),          PV_RZRO },          \
  { FLDATA (READY, hi##N##_db.ready,   0),          PV_RZRO },          \
  { FLDATA (FULL,  hi##N##_db.full ,   0),          PV_RZRO },          \
  { DRDATA (LINK,  hi##N##_db.link,   32), REG_RO + PV_LEFT },       \
  { DRDATA (BPS,   hi##N##_db.bps,    32), REG_NZ + PV_LEFT },       \
  { NULL }                                                              \
}
REG hi1_reg[] = HI_REG(1), hi2_reg[] = HI_REG(2);
REG hi3_reg[] = HI_REG(3), hi4_reg[] = HI_REG(4);

// Host Device Modifiers ...
//  These are the modifiers simh uses for the "SET MIxn" and "SHOW MIx" commands.
#define HI_MOD(N) {                                                                     \
  { 0 }                                                                                 \
}
MTAB hi1_mod[] = HI_MOD(1), hi2_mod[] = HI_MOD(2);
MTAB hi3_mod[] = HI_MOD(3), hi4_mod[] = HI_MOD(4);

// Debug modifiers for "SET HIn DEBUG = xxx" ...
DEBTAB hi_debug[] = {
  {"WARN",  IMP_DBG_WARN}, // print warnings that would otherwise be suppressed
  {"UDP",   IMP_DBG_UDP},  // print all UDP messages sent and received
  {"IO",    IMP_DBG_IOT},  // print all program I/O instructions
  {0}
};

// Host Device data ...
//   This is the primary simh structure that defines each device - it gives the
// plain text name, the addresses of the unit, register and modifier tables, and
// the addresses of all action routines (e.g. attach, reset, etc).
#define HI_DEV(HI,N,F) {                                                \
  #HI, &hi##N##_unit, hi##N##_reg, hi##N##_mod,                         \
  1, 10, 31, 1, 8, 8,                                                   \
  NULL, NULL, &hi_reset, NULL, &hi_attach, &hi_detach,                  \
  &hi##N##_dib, DEV_DISABLE|DEV_DEBUG|(F), 0, hi_debug, NULL, NULL      \
}
DEVICE hi1_dev = HI_DEV(HI1,1,DEV_DIS), hi2_dev = HI_DEV(HI2,2,DEV_DIS);
DEVICE hi3_dev = HI_DEV(HI3,3,DEV_DIS), hi4_dev = HI_DEV(HI4,4,DEV_DIS);

// Host Tables ...
//   These tables make it easy to locate the data associated with any host.
DEVICE *const hi_devices[HI_NUM] = {&hi1_dev,  &hi2_dev,  &hi3_dev,  &hi4_dev };
UNIT   *const hi_units  [HI_NUM] = {&hi1_unit, &hi2_unit, &hi3_unit, &hi4_unit};
DIB    *const hi_dibs   [HI_NUM] = {&hi1_dib,  &hi2_dib,  &hi3_dib,  &hi4_dib };
HIDB   *const hi_hidbs  [HI_NUM] = {&hi1_db,   &hi2_db,   &hi3_db,   &hi4_db  };



////////////////////////////////////////////////////////////////////////////////
//////////////////   L O W   L E V E L   F U N C T I O N S   ///////////////////
////////////////////////////////////////////////////////////////////////////////

// Find a pointer to the DEVICE, UNIT, DIB or HIDB given the host number ...
#define PDEVICE(h)     hi_devices[(h)-1]
#define PUNIT(h)       hi_units[(h)-1]
#define PDIB(h)        hi_dibs[(h)-1]
#define PHIDB(h)       hi_hidbs[(h)-1]

// These macros set and clear the interrupt request and enable flags ...
#define SET_RX_IRQ(h)  SET_EXT_INT((1u << (PDIB(h)->rxint - INT_V_EXTD)))
#define SET_TX_IRQ(h)  SET_EXT_INT((1u << (PDIB(h)->txint - INT_V_EXTD)))
#define CLR_RX_IRQ(h)  CLR_EXT_INT((1u << (PDIB(h)->rxint - INT_V_EXTD)))
#define CLR_TX_IRQ(h)  CLR_EXT_INT((1u << (PDIB(h)->txint - INT_V_EXTD)))
#define CLR_RX_IEN(h)  CLR_EXT_ENB((1u << (PDIB(h)->rxint - INT_V_EXTD)))
#define CLR_TX_IEN(h)  CLR_EXT_ENB((1u << (PDIB(h)->txint - INT_V_EXTD)))

// TRUE if the host has the specified debugging output enabled ...
#define ISHDBG(l,f)    ((PDEVICE(l)->dctrl & (f)) != 0)

// Reset receiver (clear flags AND initialize all data) ...
void hi_reset_rx (uint16 host)
{
  PHIDB(host)->iloop = PHIDB(host)->error = PHIDB(host)->enabled = FALSE;
  PHIDB(host)->ready = TRUE; // XXX
  PHIDB(host)->eom = FALSE;
  PHIDB(host)->rxtotal = 0;
  CLR_RX_IRQ(host);  CLR_RX_IEN(host);
}

// Reset transmitter (clear flags AND initialize all data) ...
void hi_reset_tx (uint16 host)
{
  PHIDB(host)->iloop = PHIDB(host)->enabled = PHIDB(host)->full = FALSE;
  PHIDB(host)->txtotal = 0;
  CLR_TX_IRQ(host);  CLR_TX_IEN(host);
}

// Get the DMC control words (starting address, end and length) for the channel.
void hi_get_dmc (uint16 dmc, uint16 *pnext, uint16 *plast, uint16 *pcount)
{
  uint16 dmcad;
  if ((dmc<DMC1) || (dmc>(DMC1+DMC_MAX-1))) {
    *pnext = *plast = *pcount = 0;  return;
  }
  dmcad = DMC_BASE + (dmc-DMC1)*2;
  *pnext = M[dmcad] & X_AMASK;  *plast = M[dmcad+1] & X_AMASK;
  *pcount = (*plast - *pnext + 1) & DMASK;
}

// Update the DMC words to show "count" words transferred.
void hi_update_dmc (uint32 dmc, uint32 count)
{
  uint16 dmcad, next;
  if ((dmc<DMC1) || (dmc>(DMC1+DMC_MAX-1))) return;
  dmcad = DMC_BASE + (dmc-DMC1)*2;
  next = M[dmcad];
  M[dmcad] = (next & DMA_IN) | ((next+count) & X_AMASK);
}

// Link error recovery ...
void hi_link_error (uint16 line)
{
  //   Any physical I/O error, either for the UDP link or a COM port, prints a
  // message and detaches the modem.  It's up to the user to decide what to do
  // after that...
  sim_printf("HI%d - UNRECOVERABLE I/O ERROR!\n", line);
  hi_reset_rx(line);  hi_reset_tx(line);
  sim_cancel(PUNIT(line));  hi_detach(PUNIT(line));
  PHIDB(line)->link = NOLINK;
}



////////////////////////////////////////////////////////////////////////////////
///////////////////   D E B U G G I N G   R O U T I N E S   ////////////////////
////////////////////////////////////////////////////////////////////////////////

// Log a modem input or output including DMC words ...
void hi_debug_hio (uint16 line, uint32 dmc, const char *ptext)
{
  uint16 next, last, count;
  if (!ISHDBG(line, IMP_DBG_IOT)) return;
  hi_get_dmc(dmc, &next, &last, &count);
  sim_debug(IMP_DBG_IOT, PDEVICE(line),
    "start %s (PC=%06o, next=%06o, last=%06o, count=%d)\n",
    ptext, PC-1, next, last, count);
}

// Log the contents of a message sent or received ...
void hi_debug_msg (uint16 line, uint16 next, uint16 count, const char *ptext)
{
  uint16 i;  char buf[CBUFSIZE];  int len = 0;
  if (!ISHDBG(line, MI_DBG_MSG)) return;
  sim_debug(MI_DBG_MSG, PDEVICE(line), "message %s (length=%d)\n", ptext, count);
  for (i = 1, len = 0;  i <= count;  ++i) {
    len += sprintf(buf+len, "%06o ", M[next+i-1]);
    if (((i & 7) == 0) || (i == count)) {
      sim_debug(MI_DBG_MSG, PDEVICE(line), "- %s\n", buf);  len = 0;
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
/////////////////   T R A N S M I T   A N D   R E C E I V E   //////////////////
////////////////////////////////////////////////////////////////////////////////

// Start the transmitter ...
void hi_start_tx (uint16 line, uint16 flags)
{
  //   This handles all the work of the "start host output" OCP, including
  // extracting the packet from H316 memory, EXCEPT for actually setting the
  // transmit done interrupt.  That's handled by the RTC polling routine after
  // a delay that we calculate..
  uint16 next, last, count;  uint32 nbits;  t_stat ret;

  //   Get the DMC words for this channel and update the next pointer as if the
  // transfer actually occurred.
  hi_get_dmc(PDIB(line)->txdmc, &next, &last, &count);
  hi_update_dmc(PDIB(line)->txdmc, count);
  hi_debug_msg (line, next, count, "sent");

  //   Transmit the data, handling both the interface loopback AND the line loop
  // back flags in the process.  Note that in particular the interface loop back
  // does NOT require that the host be attached!
  if (PHIDB(line)->iloop) {
    hi_rx_local(line, next, count);
  } else if (PHIDB(line)->link != NOLINK) {
//for (int i = 0; i < count; i ++) fprintf (stderr, "%06o\r\n", M[next + i]);
    // The host interface needs some out-of-band data bits. The format 
    // of the data going out over the wire is:
    // struct
    //   uint16 flags;
    //   uint16 data [MAXDATA - 1];
    // Put the packet into a temp buffer for assembly.
    uint16 *tmp = (uint16 *)malloc ((count + 1) * sizeof (*tmp));
    uint16 i;

    tmp [0] = flags;
    for (i = 0; i < count; i ++)
      tmp [i + 1] = M [next+i];
    ret = udp_send(PDEVICE(line), PHIDB(line)->link, tmp, count);
    free (tmp);
    if (ret != SCPE_OK && ret != 66) hi_link_error(line);
  }

// XXX the host interface is significantly faster... Need new math here.
// 1822 pg 4-9 100 KBS

  //   Do some fancy math to figure out how long, in RTC ticks, it would actually
  // take to transmit a packet of this length with a real modem and phone line.
  // Note that the "+12" is an approximation for the modem overhead, including
  // DLE, STX, ETX and checksum bytes, that would be added to the packet.
  nbits = (((uint32) count)*2UL + 12UL) * 8UL;
  PHIDB(line)->txdelay = (nbits * 1000000UL) / (PHIDB(line)->bps * rtc_interval);
  sim_debug(IMP_DBG_IOT, PDEVICE(line), "HI%d - transmit packet, length=%d, bits=%u, interval=%u, delay=%u\n", line, count, nbits, rtc_interval, PHIDB(line)->txdelay);
  // That's it - we're done until it's time for the TX done interrupt ...
  CLR_TX_IRQ(line);
}

// Poll for transmitter done interrupts ...
void hi_poll_tx (uint16 line, uint32 quantum)
{
  //   This routine is called, via the RTC service, to count down the interval
  // until the transmitter finishes.  When it hits zero, an interrupt occurs.
  if (PHIDB(line)->txdelay == 0) return;
  if (PHIDB(line)->txdelay <= quantum) {
    SET_TX_IRQ(line);  PHIDB(line)->txdelay = 0;  PHIDB(line)->txtotal++;
    sim_debug(IMP_DBG_IOT, PDEVICE(line), "transmit done (message #%d, intreq=%06o)\n", PHIDB(line)->txtotal, dev_ext_int);
  } else
    PHIDB(line)->txdelay -= quantum;
}

// Start the receiver ...
void hi_start_rx (uint16 line)
{
  //   "Starting" the receiver simply sets the RX pending flag.  Nothing else
  // needs to be done (nothing else _can_ be done!) until we actually receive
  // a real packet.

  //   We check for the case of another receive already pending, but I don't
  // think the real hardware detected this or considered it an error condition.
  if (PHIDB(line)->rxpending) {
    sim_debug(IMP_DBG_WARN,PDEVICE(line),"start input while input already pending\n");
  }
  PHIDB(line)->rxpending = TRUE;  PHIDB(line)->rxerror = FALSE;
  CLR_RX_IRQ(line);
}

// Poll for receiver data ...
void hi_poll_rx (uint16 line)
{
  //   This routine is called by hi_rx_service to poll for any packets received.
  // This is done regardless of whether a receive is pending on the line.  If
  // a packet is waiting AND a receive is pending then we'll store it and finish
  // the receive operation.  If a packet is waiting but no receive is pending
  // then the packet is discarded...
  uint16 next, last, maxbuf;  uint16 *pdata;  int16 count;
  uint16 *tmp = NULL;
  uint16 i;

  // If the modem isn't attached, then the read never completes!
  if (PHIDB(line)->link == NOLINK) return;

  // Get the DMC words for this channel, or zeros if no read is pending ...
  if (PHIDB(line)->rxpending) {
    hi_get_dmc(PDIB(line)->rxdmc, &next, &last, &maxbuf);
    pdata = &M[next];
  } else {
    next = last = maxbuf = 0;  pdata = NULL;
  }
  // Try to read a packet.  If we get nothing then just return.
  // The host interface needs some out-of-band data bits. The format 
  // of the data coming over the wire is:
  // struct
  //   uint16 flags;
  //   uint16 data [MAXDATA - 1];
  // Read the packet into a temp buffer for disassembly.
  tmp = (uint16 *)malloc (MAXDATA * sizeof (*tmp));

  count = udp_receive(PDEVICE(line), PHIDB(line)->link, tmp, maxbuf+1);
  if (count == 0) {free (tmp); return; }
  if (count < 0) {free (tmp); hi_link_error(line); return; }

  PHIDB(line)->eom = !! tmp[0] & PFLG_FINAL;
  for (i = 0; i < count - 1; i ++)
    * (pdata + i) = tmp [i + 1];
  free (tmp);
  tmp = NULL;
  // Now would be a good time to worry about whether a receive is pending!
  if (!PHIDB(line)->rxpending) {
    sim_debug(IMP_DBG_WARN, PDEVICE(line), "data received with no input pending\n");
    return;
  }

  //   We really got a packet!  Update the DMC pointers to reflect the actual
  // size of the packet received.  If the packet length would have exceeded the
  // receiver buffer, then that sets the error flag too.
  if (count > maxbuf) {
    sim_debug(IMP_DBG_WARN, PDEVICE(line), "receiver overrun (length=%d maxbuf=%d)\n", count, maxbuf);
    PHIDB(line)->rxerror = TRUE;  count = maxbuf;
  }
  hi_update_dmc(PDIB(line)->rxdmc, count);
  hi_debug_msg (line, next, count, "received");

  // Assert the interrupt request and we're done!
  SET_RX_IRQ(line);  PHIDB(line)->rxpending = FALSE;  PHIDB(line)->rxtotal++;
  sim_debug(IMP_DBG_IOT, PDEVICE(line), "receive done (message #%d, intreq=%06o)\n", PHIDB(line)->rxtotal, dev_ext_int);
}


// Receive cross patched data ...
void hi_rx_local (uint16 line, uint16 txnext, uint16 txcount)
{
  //   This routine is invoked by the hi_start_tx() function when this modem has
  // the "interface cross patch" bit set.  This flag causes the modem to talk to
  // to itself, and data sent by the transmitter goes directly to the receiver.
  // The modem is bypassed completely and in fact need not even be connected.
  // This is essentially a special case of the hi_poll_rx() routine and it's a
  // shame they don't share more code, but that's the way it is.
  // Get the DMC words for this channel, or zeros if no read is pending ...
  uint16 rxnext, rxlast, maxbuf;

  // If no read is pending, then just throw away the data ...
  if (!PHIDB(line)->rxpending) return;

  // Get the DMC words for the receiver and copy data from one buffer to the other.
  hi_get_dmc(PDIB(line)->rxdmc, &rxnext, &rxlast, &maxbuf);
  if (txcount > maxbuf) {txcount = maxbuf;  PHIDB(line)->rxerror = TRUE;}
  memmove(&M[rxnext], &M[txnext], txcount * sizeof(uint16));

  // Update the receiver DMC pointers, assert IRQ and we're done!
  hi_update_dmc(PDIB(line)->rxdmc, txcount);
  hi_debug_msg (line, rxnext, txcount, "received");
  SET_RX_IRQ(line);  PHIDB(line)->rxpending = FALSE;  PHIDB(line)->rxtotal++;
  sim_debug(IMP_DBG_IOT, PDEVICE(line), "receive done (message #%d, intreq=%06o)\n", PHIDB(line)->rxtotal, dev_ext_int);
}



////////////////////////////////////////////////////////////////////////////////
////////////   I / O   I N S T R U C T I O N   E M U L A T I O N   /////////////
////////////////////////////////////////////////////////////////////////////////

// Host specific I/O routines ...
int32 hi1_io(int32 inst, int32 fnc, int32 dat, int32 dev)  {return hi_io(1, inst, fnc, dat, dev);}
int32 hi2_io(int32 inst, int32 fnc, int32 dat, int32 dev)  {return hi_io(2, inst, fnc, dat, dev);}
int32 hi3_io(int32 inst, int32 fnc, int32 dat, int32 dev)  {return hi_io(3, inst, fnc, dat, dev);}
int32 hi4_io(int32 inst, int32 fnc, int32 dat, int32 dev)  {return hi_io(4, inst, fnc, dat, dev);}

// Common I/O simulation routine ...
int32 hi_io (uint16 host, int32 inst, int32 fnc, int32 dat, int32 dev)
{
  //   This routine is invoked by the CPU module whenever the code executes any
  // I/O instruction (OCP, SKS, INA or OTA) with one of our modem's device
  // address.

  // OCP (output control pulse) initiates various modem operations ...
  if (inst == ioOCP) {
    switch (fnc) {
      case 000:
        // HnROUT - start regular host output ...
        hi_debug_hio(host, PDIB(host)->txdmc, "output");
        hi_start_tx(host, 0);  return dat;
      case 001:
        // HnIN - start host input ...
        hi_debug_hio(host, PDIB(host)->rxdmc, "input");
        hi_start_rx(host);  return dat;
        return dat;
      case 002:
        // HnFOUT - start final host output ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "start final output (PC=%06o)\n", PC-1);
        hi_start_tx(host, PFLG_FINAL);  return dat;
        return dat;
      case 003:
        // HnXP - cross patch ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "enable cross patch (PC=%06o)\n", PC-1);
        PHIDB(host)->iloop = TRUE;
        udp_set_link_loopback (PDEVICE(host), PHIDB(host)->link, TRUE);
        return dat;
      case 004:
        // HnUNXP - un-cross patch ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "disable cross patch (PC=%06o)\n", PC-1);
        PHIDB(host)->iloop = FALSE;
        udp_set_link_loopback (PDEVICE(host), PHIDB(host)->link, FALSE);
        return dat;
      case 005:
        sim_printf("HnENAB unimp.\n");
        // HnENAB - enable ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "enable host (PC=%06o)\n", PC-1);
        return dat;
    }

  // SKS (skip) tests various modem conditions ...
  } else if (inst == ioSKS) {
    switch (fnc) {
      case 000:
        // HnERR - skip on host error ...
        sim_debug(IMP_DBG_IOT,PDEVICE(host),"skip on error (PC=%06o, %s)\n",
          PC-1, PHIDB(host)->rxerror ? "SKIP" : "NOSKIP");
        return  PHIDB(host)->rxerror ? IOSKIP(dat) : dat;
      case 001:
        // HnRDY - skip on host ready ...
        //sim_debug(IMP_DBG_IOT, PDEVICE(host), "skip on ready (PC=%06o %s)\n", PC-1, PHIDB(host)->ready ? "SKIP" : "NOSKIP");
        sim_printf("HnRDY unimpl.; always ready\n");
        return  PHIDB(host)->ready ? IOSKIP(dat) : dat;
      case 002:
        // HnEOM - skip on end of message ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "skip on end of message (PC=%06o %s)\n", PC-1, PHIDB(host)->eom ? "SKIP" : "NOSKIP");
        return  PHIDB(host)->eom ? IOSKIP(dat) : dat;
        return dat;
      case 005:
        // HnFULL - skip on host buffer full ...
        sim_printf("HnFULL unimp.\n");
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "skip on buffer full (PC=%06o %s)\n", PC-1, "NOSKIP");
        return dat;
    }
  }

  // Anything else is an error...
  sim_debug(IMP_DBG_WARN, PDEVICE(host), "UNIMPLEMENTED I/O (PC=%06o, instruction=%o, function=%02o)\n", PC-1, inst, fnc);
  return IOBADFNC(dat);
}




////////////////////////////////////////////////////////////////////////////////
///////////////////   H O S T   E V E N T   S E R V I C E   ////////////////////
////////////////////////////////////////////////////////////////////////////////

// Receiver service ...
t_stat hi_rx_service (UNIT *uptr)
{
  //   This is the standard simh "service" routine that's called when an event
  // queue entry expires.  It just polls the receiver and reschedules itself.
  // That's it!
  uint16 line = uptr->hline;
  hi_poll_rx(line);
  sim_activate(uptr, uptr->wait);
  return SCPE_OK;
}

// Transmitter service ...
t_stat hi_tx_service (uint32 quantum)
{
  //   This is the special transmitter service routine that's called by the RTC
  // service every time the RTC is updated.  This routine polls ALL the modem
  // transmitters (or at least any which are active) and figures out whether it
  // is time for an interrupt.
  uint32 i;
  for (i = 1;  i <= HI_NUM;  ++i) hi_poll_tx(i, quantum);
  return SCPE_OK;
}



////////////////////////////////////////////////////////////////////////////////
///////////////   D E V I C E   A C T I O N   C O M M A N D S   ////////////////
////////////////////////////////////////////////////////////////////////////////

// Reset routine ...
t_stat hi_reset (DEVICE *dptr)
{
  // simh calls this routine for the RESET command ...
  UNIT *uptr = dptr->units;
  uint16 host= uptr->hline;
  hi_reset_rx(host);  hi_reset_tx(host);
  sim_cancel(uptr);
  if ((uptr->flags & UNIT_ATT) != 0) sim_activate(uptr, uptr->wait);
  return SCPE_OK;
}

// Attach (connect) ...
t_stat hi_attach (UNIT *uptr, CONST char *cptr)
{
  //   simh calls this routine for (what else?) the ATTACH command.  There are
  // three distinct formats for ATTACH -
  //
  //    ATTACH -p HIn COHnn          - attach MIn to a physical COM port
  //    ATTACH HIn llll:w.x.y.z:rrrr - connect via UDP to a remote simh host
  //
  t_stat ret;  char *pfn;  uint16 host = uptr->hline;
  t_bool fport = sim_switches & SWMASK('P');

  // If we're already attached, then detach ...
  if ((uptr->flags & UNIT_ATT) != 0) detach_unit(uptr);

  // The physical (COM port) attach isn't implemented yet ...
  if (fport)
    return sim_messagef(SCPE_ARG,"HI%d - physical COM support is not yet implemented\n", host);

  //   Make a copy of the "file name" argument.  udp_create() actually modifies
  // the string buffer we give it, so we make a copy now so we'll have something
  // to display in the "SHOW HIn ..." command.
  pfn = (char *) calloc (CBUFSIZE, sizeof (char));
  if (pfn == NULL) return SCPE_MEM;
  strncpy (pfn, cptr, CBUFSIZE);

  // Create the UDP connection.
  ret = udp_create(PDEVICE(host), cptr, &(PHIDB(host)->link));
  if (ret != SCPE_OK) {free(pfn);  return ret;};

  // Reset the flags and start polling ...
  uptr->flags |= UNIT_ATT;  uptr->filename = pfn;
  return hi_reset(find_dev_from_unit(uptr));
}

// Detach (connect) ...
t_stat hi_detach (UNIT *uptr)
{
  //   simh calls this routine for (you guessed it!) the DETACH command.  This
  // disconnects the modem from any UDP connection or COM port and effectively
  // makes the modem "off line".  A disconnected modem acts like a real modem
  // with its phone line unplugged.
  t_stat ret;  uint16 line = uptr->hline;
  if ((uptr->flags & UNIT_ATT) == 0) return SCPE_OK;
  ret = udp_release(PDEVICE(line), PHIDB(line)->link);
  if (ret != SCPE_OK) return ret;
  PHIDB(line)->link = NOLINK;  uptr->flags &= ~UNIT_ATT;
  free (uptr->filename);  uptr->filename = NULL;
  return hi_reset(PDEVICE(line));
}


#endif // #ifdef VM_IMPTIP from the very top
