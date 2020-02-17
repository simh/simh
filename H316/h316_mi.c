/* h316_mi.c- BBN ARPAnet IMP/TIP Modem Interface
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


   REVISION HISTORY

   mi           modem interface

   21-May-13    RLA     New file


   OVERVIEW

   The modem interface is one of the BBN engineered devices unique to the
   ARPAnet IMP/TIP.  The original hardware was a full duplex synchronous serial
   line interface operating at 56k bps.  The hardware was fairly smart and was
   able to handle line synchronization (SYN), packet start (STX) and end (ETX),
   and data escape (DLE) autonomously.  Data is transferred directly to and
   from H316 main memory using the DMC mechanism.  The modem interface also
   calculated a 24 bit "parity" (and by that I assume they meant some form of
   CRC) value.  This was automatically appended to the end of the transmitted
   data and automatically verified by the receiving modem.

   CONNECTIONS

   This module provides two basic options for emulating the modem.  Option 1
   takes the data packets from H316 memory, wraps them in a UDP packet, and
   sends them to another simh instance.  The remote simh then unwraps the data
   packet and loads it directly into H316 memory.  In this instance,
   synchronization, start of text/end of text, and data escapes are pointless
   and are not used.  The words are simply moved verbatim from one H316 to
   another.

   The other option is to logically connect the emulated modem to a physical
   serial port on this PC.  In that case we attempt to emulate the actions
   of the original modem as closely as possible, including the synchronization,
   start of text/end of text and data escape characters.  Synchronization is
   pointless on an asynchronous interface, of course, but we do it anyway in
   the interest of compatability.  We also attempt to calculate a 24 bit CRC
   using (as best I can determine) the same algorithm as the original modems.

   MULTIPLE INSTANCES

   Each IMP can support up to five modem lines, and fitting this into simh
   presents an architectural problem.  The temptation is to treat all modems as
   one device with multiple units (each unit corresponding to one line), but
   that's a problem.  The simh view of units is like a disk or tape - there's
   a single controller that has one IO address, one interrupt and one DMA/DMC
   channel.  That controller then has multiple units attached to it that are
   selected by bits in a controller register and all units share the same
   IO, interrupt and DMA/DMC assignments.

   The modems aren't like that at all - each of the five cards is completely
   independent with its own distinct IO address, interrupt and DMC assignments.
   It's analagous to five instances of the same controller installed in the
   machine, but simh unfortunately has limited support for multiple instances
   of the same controller.  The few instances of prior art in simh that I can
   find (e.g. xq, xqb on the PDP11/VAX) have just been done ad hoc by
   duplicating all the device data.  Rather than rewrite simh, that's the
   approach I took here, even though with five instances it gets awkward.

   POLLING AND SERVICE

   The IMP software turns out to be extraordinarily sensitive to modem timing.
   It actually goes to the trouble of measuring the effective line speed by
   using the RTC to time how long it takes to send a message, and one thing that
   especially annoys the IMP are variations in the effective line speed.  They
   had a lot of trouble with AT&T Long Lines back in the Old Days, and the IMP
   has quite a bit of code to monitor line quality. Even fairly minor variations
   in speed will cause it to mark the line as "down" and sent a trouble report
   back to BBN.

   To combat this, we actually let the RTC code time the transmitter interrupts.
   When the IMP software does a "start modem output" OCP the entire packet will
   be extracted from H316 memory and transmitted via UDP at that instant, BUT
   the transmitter done interrupt will be deferred.  The delay is computed from
   the length of the packet and the simulated line speed, and then the RTC is
   used to count down the interval. When the time expires, the interrupt request
   is generated.  It's unfortunate to have to couple the RTC and the modem in
   this way, but since the IMP code is using the RTC to measure the line speed
   AND since the RTC determines when the transmit done occurs, it guarantees
   that the IMP always sees exactly the same delay.

   The modem receiver is completely independent of the transmitter and is polled
   by the usual simh event queue mechanism and mi_service() routine. When the
   IMP code executes a "start modem input" OCP a read pending flag is set in the
   modem status but nothing else occurs.  Each poll checks the UDP socket for
   incoming data and, if a packet was received AND a read operation is pending,
   then the read completes that moment and the interrupt request is asserted.
   The UDP socket is polled regardless of whether a read is pending and if data
   arrives without a read then it's discarded.  That's exactly what a real modem
   would do.

   ERROR HANDLING

   Transmitter error handling is easy - fatal errors print a message and abort
   the simulation, but any other errors are simply ignored.  The IMP modems had
   no kind of error dection on the transmitter side and no way to report them
   anyway so we do the same.  Any data packet associated with the error is just
   discarded.  In particular with both UDP and COM ports there's no way to tell
   whether anybody is on the other end listening, so even packets that are
   successfully transmitted may disappear into the ether.  This isn't a problem
   for the IMP - the real world was like that too and the IMP is able to handle
   retransmitting packets without our help.

   Receiver errors set the error flag in the modem status; this flag can be
   tested and cleared by the "skip on modem error" SKS instruction.  The only
   receiver error that can be detected is buffer overrun (i.e. the sender's
   message was longer than the receiver's buffer).  With a serial connection
   checksum errors are also possible, but those never occur with UDP.

   Transmitting or receiving on a modem that's not attached isn't an error - it
   simply does nothing. It's analogous to a modem with the phone line unplugged.
   Hard I/O errors for UDP or COM ports print an error message and then detach
   the modem connection.  It's up to the user to interrupt the simulation and
   reattach if he wants to try again.

   STATE

   Modem state is maintained in the following variables -

        RXPOLL  24      receiver polling interval
        RXPEND   1      an input operation is pending
        RXERR    1      receiver error flag
        RXIEN    1      receiver interrupt enable
        RXIRQ    1      receiver interrupt request
        RXTOT   32      count of total messages received
        TXDLY   32      RTC ticks until TX done interrupt
        TXIEN    1      transmitter interrupt enable
        TXIRQ    1      transmitter interrupt request
        TXTOT   32      count of total messages transmitted
        LINKNO  32      link number for h316_udp module
        BPS     32      simulated bps for UDP delay calculations
                        actual baud rate for physical COM ports
        ILOOP    1      interface (local) loopback enabled
        RLOOP    1      remote (line) loopback enabled

   Most of these values will be found in the Modem Information Data Block (aka
   "MIDB") but a few are stored elsewhere (e.g. IRQ/IEN are in the CPU's dev_int
   and dev_enb vectors).

   TODO

   Implement checksum handling
   Implement remote loopback
*/
#ifdef VM_IMPTIP
#include "h316_defs.h"          // H316 emulator definitions
#include "h316_imp.h"           // ARPAnet IMP/TIP definitions

// Externals from other parts of simh ...
extern uint16 dev_ext_int, dev_ext_enb; // current IRQ and IEN bit vectors
extern int32 PC;                        // current PC (for debug messages)
extern int32 stop_inst;                 // needed by IOBADFNC()
extern uint16 M[];                      // main memory (for DMC access)

// Forward declarations ...
int32  mi_io (uint16 line, int32 inst, int32 fnc, int32 dat, int32 dev);
int32 mi1_io (int32 inst, int32 fnc, int32 dat, int32 dev);
int32 mi2_io (int32 inst, int32 fnc, int32 dat, int32 dev);
int32 mi3_io (int32 inst, int32 fnc, int32 dat, int32 dev);
int32 mi4_io (int32 inst, int32 fnc, int32 dat, int32 dev);
int32 mi5_io (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat mi_rx_service (UNIT *uptr);
void mi_rx_local (uint16 line, uint16 txnext, uint16 txcount);
t_stat mi_reset (DEVICE *dptr);
t_stat mi_attach (UNIT *uptr, CONST char *cptr);
t_stat mi_detach (UNIT *uptr);
t_stat mi_set_loopback (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat mi_show_loopback (FILE *st, UNIT *uptr, int32 val, CONST void *desc);



////////////////////////////////////////////////////////////////////////////////
//////////////////////   D A T A    S T R U C T U R E S   //////////////////////
////////////////////////////////////////////////////////////////////////////////

//   simh requires several data structures for every device - a DIB, one or more
// UNITS, a modifier table, a register list, and a device definition.  The sit-
// uation here is even more complicated because we have five identical modems to
// define, and so lots of clever macros are used to handle the repetition and
// save some typing.

// Modem Information Data Blocks ...
//   The MIDB is our own internal data structure for each modem.  It keeps data
// about the current state, COM port, UDP connection, etc.
#define MI_MIDB(N) {FALSE, FALSE, 0, 0, 0, FALSE, FALSE, NOLINK, MI_TXBPS}
MIDB mi1_db = MI_MIDB(1), mi2_db = MI_MIDB(2);
MIDB mi3_db = MI_MIDB(3), mi4_db = MI_MIDB(4);
MIDB mi5_db = MI_MIDB(5);

// Modem Device Information Blocks ...
//   The DIB is the structure simh uses to keep track of the device IO address
// and IO service routine.  It can also hold the DMC channel, but we don't use
// that because it's unit specific.
#define MI_DIB(N) {MI##N, 1, MI##N##_RX_DMC, MI##N##_TX_DMC, \
                   INT_V_MI##N##RX, INT_V_MI##N##TX, &mi##N##_io, N}
DIB mi1_dib = MI_DIB(1), mi2_dib = MI_DIB(2);
DIB mi3_dib = MI_DIB(3), mi4_dib = MI_DIB(4);
DIB mi5_dib = MI_DIB(5);

// Modem Device Unit data ...
//  simh uses the unit data block primarily to schedule device service events.
#define mline  u3       // our modem line number is stored in user data 3
#define MI_UNIT(N) {UDATA (&mi_rx_service, UNIT_ATTABLE, 0), MI_RXPOLL, N, 0, 0, 0}
UNIT mi1_unit = MI_UNIT(1), mi2_unit = MI_UNIT(2);
UNIT mi3_unit = MI_UNIT(3), mi4_unit = MI_UNIT(4);
UNIT mi5_unit = MI_UNIT(5);

// Modem Device Registers ...
//   These are the simh device "registers" - they c can be viewed with the
// "EXAMINE MIn STATE" command and modified by "DEPOSIT MIn ..."
#define MI_REG(N) {                                                     \
  { DRDATA (RXPOLL, mi##N##_unit.wait,   24), REG_NZ + PV_LEFT },       \
  { FLDATA (RXPEND, mi##N##_db.rxpending, 0), REG_RO + PV_RZRO },       \
  { FLDATA (RXERR,  mi##N##_db.rxerror,   0),          PV_RZRO },       \
  { FLDATA (RXIEN,  dev_ext_enb, INT_V_MI##N##RX-INT_V_EXTD)   },       \
  { FLDATA (RXIRQ,  dev_ext_int, INT_V_MI##N##RX-INT_V_EXTD)   },       \
  { DRDATA (RXTOT,  mi##N##_db.rxtotal,  32), REG_RO + PV_LEFT },       \
  { DRDATA (TXDLY,  mi##N##_db.txdelay,  32),          PV_LEFT },       \
  { FLDATA (TXIEN,  dev_ext_enb, INT_V_MI##N##TX-INT_V_EXTD)   },       \
  { FLDATA (TXIRQ,  dev_ext_int, INT_V_MI##N##TX-INT_V_EXTD)   },       \
  { DRDATA (TXTOT,  mi##N##_db.txtotal,  32), REG_RO + PV_LEFT },       \
  { DRDATA (LINK,   mi##N##_db.link,     32), REG_RO + PV_LEFT },       \
  { DRDATA (BPS,    mi##N##_db.bps,      32), REG_NZ + PV_LEFT },       \
  { FLDATA (LLOOP,  mi##N##_db.lloop,     0), REG_RO + PV_RZRO },       \
  { FLDATA (ILOOP,  mi##N##_db.iloop,     0), REG_RO + PV_RZRO },       \
  { NULL }                                                              \
}
REG mi1_reg[] = MI_REG(1), mi2_reg[] = MI_REG(2);
REG mi3_reg[] = MI_REG(3), mi4_reg[] = MI_REG(4);
REG mi5_reg[] = MI_REG(5);

// Modem Device Modifiers ...
//  These are the modifiers simh uses for the "SET MIn" and "SHOW MIn" commands.
#define MI_MOD(N) {                                                                                        \
  { MTAB_XTD|MTAB_VDV, 0, "LOOPBACK",      "LOOPINTERFACE",   &mi_set_loopback, &mi_show_loopback, NULL }, \
  { MTAB_XTD|MTAB_VDV, 1, NULL,            "NOLOOPINTERFACE", &mi_set_loopback, NULL,              NULL }, \
  { MTAB_XTD|MTAB_VDV, 2, NULL,            "LOOPLINE",        &mi_set_loopback, NULL,              NULL }, \
  { MTAB_XTD|MTAB_VDV, 3, NULL,            "NOLOOPLINE",      &mi_set_loopback, NULL,              NULL }, \
  { 0 }                                                                                                    \
}
MTAB mi1_mod[] = MI_MOD(1), mi2_mod[] = MI_MOD(2);
MTAB mi3_mod[] = MI_MOD(3), mi4_mod[] = MI_MOD(4);
MTAB mi5_mod[] = MI_MOD(5);

// Debug modifiers for "SET MIn DEBUG = xxx" ...
DEBTAB mi_debug[] = {
  {"WARN",  IMP_DBG_WARN}, // print warnings that would otherwise be suppressed
  {"UDP",   IMP_DBG_UDP},  // print all UDP messages sent and received
  {"IO",    IMP_DBG_IOT},  // print all program I/O instructions
  {"MSG",    MI_DBG_MSG},  // decode and print all messages
  {0}
};

// Modem Device data ...
//   This is the primary simh structure that defines each device - it gives the
// plain text name, the addresses of the unit, register and modifier tables, and
// the addresses of all action routines (e.g. attach, reset, etc).
#define MI_DEV(MI,N,F) {                                                \
  #MI, &mi##N##_unit, mi##N##_reg, mi##N##_mod,                         \
  1, 10, 31, 1, 8, 8,                                                   \
  NULL, NULL, &mi_reset, NULL, &mi_attach, &mi_detach,                  \
  &mi##N##_dib, DEV_DISABLE|DEV_DEBUG|(F), 0, mi_debug, NULL, NULL      \
}
DEVICE mi1_dev = MI_DEV(MI1,1,DEV_DIS), mi2_dev = MI_DEV(MI2,2,DEV_DIS);
DEVICE mi3_dev = MI_DEV(MI3,3,DEV_DIS), mi4_dev = MI_DEV(MI4,4,DEV_DIS);
DEVICE mi5_dev = MI_DEV(MI5,5,DEV_DIS);

// Modem Tables ...
//   These tables make it easy to locate the data associated with any line.
DEVICE *const mi_devices[MI_NUM] = {&mi1_dev,  &mi2_dev,  &mi3_dev,  &mi4_dev,  &mi5_dev };
UNIT   *const mi_units  [MI_NUM] = {&mi1_unit, &mi2_unit, &mi3_unit, &mi4_unit, &mi5_unit};
DIB    *const mi_dibs   [MI_NUM] = {&mi1_dib,  &mi2_dib,  &mi3_dib,  &mi4_dib,  &mi5_dib };
MIDB   *const mi_midbs  [MI_NUM] = {&mi1_db,   &mi2_db,   &mi3_db,   &mi4_db,   &mi5_db  };



////////////////////////////////////////////////////////////////////////////////
//////////////////   L O W   L E V E L   F U N C T I O N S   ///////////////////
////////////////////////////////////////////////////////////////////////////////

// Find a pointer to the DEVICE, UNIT, DIB or MIDB given the line number ...
#define PDEVICE(l)     mi_devices[(l)-1]
#define PUNIT(l)       mi_units[(l)-1]
#define PDIB(l)        mi_dibs[(l)-1]
#define PMIDB(l)       mi_midbs[(l)-1]

// These macros set and clear the interrupt request and enable flags ...
#define SET_RX_IRQ(l)  SET_EXT_INT((1u << (PDIB(l)->rxint - INT_V_EXTD)))
#define SET_TX_IRQ(l)  SET_EXT_INT((1u << (PDIB(l)->txint - INT_V_EXTD)))
#define CLR_RX_IRQ(l)  CLR_EXT_INT((1u << (PDIB(l)->rxint - INT_V_EXTD)))
#define CLR_TX_IRQ(l)  CLR_EXT_INT((1u << (PDIB(l)->txint - INT_V_EXTD)))
#define CLR_RX_IEN(l)  CLR_EXT_ENB((1u << (PDIB(l)->rxint - INT_V_EXTD)))
#define CLR_TX_IEN(l)  CLR_EXT_ENB((1u << (PDIB(l)->txint - INT_V_EXTD)))

// TRUE if the line has the specified debugging output enabled ...
#define ISLDBG(l,f)    ((PDEVICE(l)->dctrl & (f)) != 0)

// Reset receiver (clear flags AND initialize all data) ...
void mi_reset_rx (uint16 line)
{
  PMIDB(line)->iloop = PMIDB(line)->lloop = FALSE;
  udp_set_link_loopback (PDEVICE(line), PMIDB(line)->link, FALSE);
  PMIDB(line)->rxerror = PMIDB(line)->rxpending = FALSE;
  PMIDB(line)->rxtotal = 0;
  CLR_RX_IRQ(line);  CLR_RX_IEN(line);
}

// Reset transmitter (clear flags AND initialize all data) ...
void mi_reset_tx (uint16 line)
{
  PMIDB(line)->iloop = PMIDB(line)->lloop = FALSE;
  udp_set_link_loopback (PDEVICE(line), PMIDB(line)->link, FALSE);
  PMIDB(line)->txtotal = PMIDB(line)->txdelay = 0;
  CLR_TX_IRQ(line);  CLR_TX_IEN(line);
}

// Get the DMC control words (starting address, end and length) for the channel.
void mi_get_dmc (uint16 dmc, uint16 *pnext, uint16 *plast, uint16 *pcount)
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
void mi_update_dmc (uint32 dmc, uint32 count)
{
  uint16 dmcad, next;
  if ((dmc<DMC1) || (dmc>(DMC1+DMC_MAX-1))) return;
  dmcad = DMC_BASE + (dmc-DMC1)*2;
  next = M[dmcad];
  M[dmcad] = (next & DMA_IN) | ((next+count) & X_AMASK);
}

// Link error recovery ...
void mi_link_error (uint16 line)
{
  //   Any physical I/O error, either for the UDP link or a COM port, prints a
  // message and detaches the modem.  It's up to the user to decide what to do
  // after that...
  sim_printf("MI%d - UNRECOVERABLE I/O ERROR!\n", line);
  mi_reset_rx(line);  mi_reset_tx(line);
  sim_cancel(PUNIT(line));  mi_detach(PUNIT(line));
  PMIDB(line)->link = NOLINK;
}



////////////////////////////////////////////////////////////////////////////////
///////////////////   D E B U G G I N G   R O U T I N E S   ////////////////////
////////////////////////////////////////////////////////////////////////////////

// Log a modem input or output including DMC words ...
void mi_debug_mio (uint16 line, uint32 dmc, const char *ptext)
{
  uint16 next, last, count;
  if (!ISLDBG(line, IMP_DBG_IOT)) return;
  mi_get_dmc(dmc, &next, &last, &count);
  sim_debug(IMP_DBG_IOT, PDEVICE(line),
    "start %s (PC=%06o, next=%06o, last=%06o, count=%d)\n",
    ptext, PC-1, next, last, count);
}

// Log the contents of a message sent or received ...
void mi_debug_msg (uint16 line, uint16 next, uint16 count, const char *ptext)
{
  uint16 i;  char buf[CBUFSIZE];  int len = 0;
  if (!ISLDBG(line, MI_DBG_MSG)) return;
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
void mi_start_tx (uint16 line)
{
  //   This handles all the work of the "start modem output" OCP, including
  // extracting the packet from H316 memory, EXCEPT for actually setting the
  // transmit done interrupt.  That's handled by the RTC polling routine after
  // a delay that we calculate..
  uint16 next, last, count;  uint32 nbits;  t_stat ret;

  //   Get the DMC words for this channel and update the next pointer as if the
  // transfer actually occurred.
  mi_get_dmc(PDIB(line)->txdmc, &next, &last, &count);
  mi_update_dmc(PDIB(line)->txdmc, count);
  mi_debug_msg (line, next, count, "sent");

  //   Transmit the data, handling both the interface loopback AND the line loop
  // back flags in the process.  Note that in particular the interface loop back
  // does NOT require that the modem be attached!
  if (PMIDB(line)->iloop) {
    mi_rx_local(line, next, count);
  } else if (PMIDB(line)->link != NOLINK) {
    ret = udp_send(PDEVICE(line), PMIDB(line)->link, &M[next], count);
    if (ret != SCPE_OK) mi_link_error(line);
  }

  //   Do some fancy math to figure out how long, in RTC ticks, it would actually
  // take to transmit a packet of this length with a real modem and phone line.
  // Note that the "+12" is an approximation for the modem overhead, including
  // DLE, STX, ETX and checksum bytes, that would be added to the packet.
  nbits = (((uint32) count)*2UL + 12UL) * 8UL;
  PMIDB(line)->txdelay = (nbits * 1000000UL) / (PMIDB(line)->bps * rtc_interval);
  //fprintf(stderr,"MI%d - transmit packet, length=%d, bits=%ld, interval=%ld, delay=%ld\n", line, count, nbits, rtc_interval, PMIDB(line)->txdelay);

  // That's it - we're done until it's time for the TX done interrupt ...
  CLR_TX_IRQ(line);
}

// Poll for transmitter done interrupts ...
void mi_poll_tx (uint16 line, uint32 quantum)
{
  //   This routine is called, via the RTC service, to count down the interval
  // until the transmitter finishes.  When it hits zero, an interrupt occurs.
  if (PMIDB(line)->txdelay == 0) return;
  if (PMIDB(line)->txdelay <= quantum) {
    SET_TX_IRQ(line);  PMIDB(line)->txdelay = 0;  PMIDB(line)->txtotal++;
    sim_debug(IMP_DBG_IOT, PDEVICE(line), "transmit done (message #%d, intreq=%06o)\n", PMIDB(line)->txtotal, dev_ext_int);
  } else
    PMIDB(line)->txdelay -= quantum;
}

// Start the receiver ...
void mi_start_rx (uint16 line)
{
  //   "Starting" the receiver simply sets the RX pending flag.  Nothing else
  // needs to be done (nothing else _can_ be done!) until we actually receive
  // a real packet.

  //   We check for the case of another receive already pending, but I don't
  // think the real hardware detected this or considered it an error condition.
  if (PMIDB(line)->rxpending) {
    sim_debug(IMP_DBG_WARN,PDEVICE(line),"start input while input already pending\n");
  }
  PMIDB(line)->rxpending = TRUE;  PMIDB(line)->rxerror = FALSE;
  CLR_RX_IRQ(line);
}

// Poll for receiver data ...
void mi_poll_rx (uint16 line)
{
  //   This routine is called by mi_service to poll for any packets received.
  // This is done regardless of whether a receive is pending on the line.  If
  // a packet is waiting AND a receive is pending then we'll store it and finish
  // the receive operation.  If a packet is waiting but no receive is pending
  // then the packet is discarded...
  uint16 next, last, maxbuf;  uint16 *pdata;  int16 count;

  // If the modem isn't attached, then the read never completes!
  if (PMIDB(line)->link == NOLINK) return;

  // Get the DMC words for this channel, or zeros if no read is pending ...
  if (PMIDB(line)->rxpending) {
    mi_get_dmc(PDIB(line)->rxdmc, &next, &last, &maxbuf);
    pdata = &M[next];
  } else {
    next = last = maxbuf = 0;  pdata = NULL;
  }
  // Try to read a packet.  If we get nothing then just return.
  count = udp_receive(PDEVICE(line), PMIDB(line)->link, pdata, maxbuf);
  if (count == 0) return;
  if (count < 0) {mi_link_error(line);  return;}

  // Now would be a good time to worry about whether a receive is pending!
  if (!PMIDB(line)->rxpending) {
    sim_debug(IMP_DBG_WARN, PDEVICE(line), "data received with no input pending\n");
    return;
  }

  //   We really got a packet!  Update the DMC pointers to reflect the actual
  // size of the packet received.  If the packet length would have exceeded the
  // receiver buffer, then that sets the error flag too.
  if (count > maxbuf) {
    sim_debug(IMP_DBG_WARN, PDEVICE(line), "receiver overrun (length=%d maxbuf=%d)\n", count, maxbuf);
    PMIDB(line)->rxerror = TRUE;  count = maxbuf;
  }
  mi_update_dmc(PDIB(line)->rxdmc, count);
  mi_debug_msg (line, next, count, "received");

  // Assert the interrupt request and we're done!
  SET_RX_IRQ(line);  PMIDB(line)->rxpending = FALSE;  PMIDB(line)->rxtotal++;
  sim_debug(IMP_DBG_IOT, PDEVICE(line), "receive done (message #%d, intreq=%06o)\n", PMIDB(line)->rxtotal, dev_ext_int);
}

// Receive cross patched data ...
void mi_rx_local (uint16 line, uint16 txnext, uint16 txcount)
{
  //   This routine is invoked by the mi_start_tx() function when this modem has
  // the "interface cross patch" bit set.  This flag causes the modem to talk to
  // to itself, and data sent by the transmitter goes directly to the receiver.
  // The modem is bypassed completely and in fact need not even be connected.
  // This is essentially a special case of the mi_poll_rx() routine and it's a
  // shame they don't share more code, but that's the way it is.
  // Get the DMC words for this channel, or zeros if no read is pending ...
  uint16 rxnext, rxlast, maxbuf;

  // If no read is pending, then just throw away the data ...
  if (!PMIDB(line)->rxpending) return;

  // Get the DMC words for the receiver and copy data from one buffer to the other.
  mi_get_dmc(PDIB(line)->rxdmc, &rxnext, &rxlast, &maxbuf);
  if (txcount > maxbuf) {txcount = maxbuf;  PMIDB(line)->rxerror = TRUE;}
  memmove(&M[rxnext], &M[txnext], txcount * sizeof(uint16));

  // Update the receiver DMC pointers, assert IRQ and we're done!
  mi_update_dmc(PDIB(line)->rxdmc, txcount);
  mi_debug_msg (line, rxnext, txcount, "received");
  SET_RX_IRQ(line);  PMIDB(line)->rxpending = FALSE;  PMIDB(line)->rxtotal++;
  sim_debug(IMP_DBG_IOT, PDEVICE(line), "receive done (message #%d, intreq=%06o)\n", PMIDB(line)->rxtotal, dev_ext_int);
}



////////////////////////////////////////////////////////////////////////////////
////////////   I / O   I N S T R U C T I O N   E M U L A T I O N   /////////////
////////////////////////////////////////////////////////////////////////////////

// Line specific I/O routines ...
//   Unfortunately simh doesn't pass the I/O emulation routine any data about the
// device except for its device address. In particular, it doesn't pass a pointer
// to the device or unit data blocks, so we're on our own to find those.  Rather
// than a search on the device address, we just provide a separate I/O routine
// for each modem line. All they do is call the common I/O routine with an extra
// parameter - problem solved!
int32 mi1_io(int32 inst, int32 fnc, int32 dat, int32 dev)  {return mi_io(1, inst, fnc, dat, dev);}
int32 mi2_io(int32 inst, int32 fnc, int32 dat, int32 dev)  {return mi_io(2, inst, fnc, dat, dev);}
int32 mi3_io(int32 inst, int32 fnc, int32 dat, int32 dev)  {return mi_io(3, inst, fnc, dat, dev);}
int32 mi4_io(int32 inst, int32 fnc, int32 dat, int32 dev)  {return mi_io(4, inst, fnc, dat, dev);}
int32 mi5_io(int32 inst, int32 fnc, int32 dat, int32 dev)  {return mi_io(5, inst, fnc, dat, dev);}

// Common I/O simulation routine ...
int32 mi_io (uint16 line, int32 inst, int32 fnc, int32 dat, int32 dev)
{
  //   This routine is invoked by the CPU module whenever the code executes any
  // I/O instruction (OCP, SKS, INA or OTA) with one of our modem's device
  // address.

  // OCP (output control pulse) initiates various modem operations ...
  if (inst == ioOCP) {
    switch (fnc) {
      case 000:
        // MnOUT - start modem output ...
        mi_debug_mio(line, PDIB(line)->txdmc, "output");
        mi_start_tx(line);  return dat;
      case 001:
        // MnUNXP - un-cross patch modem ...
        sim_debug(IMP_DBG_IOT,PDEVICE(line),"un-cross patch modem (PC=%06o)\n", PC-1);
        PMIDB(line)->iloop = PMIDB(line)->lloop = FALSE;  
        udp_set_link_loopback (PDEVICE(line), PMIDB(line)->link, FALSE);
        return dat;
      case 002:
        // MnLXP - enable line cross patch ...
        sim_debug(IMP_DBG_IOT,PDEVICE(line),"enable line cross patch (PC=%06o)\n", PC-1);
        PMIDB(line)->lloop = TRUE;  
        udp_set_link_loopback (PDEVICE(line), PMIDB(line)->link, TRUE);
        PMIDB(line)->iloop = FALSE;  return dat;
      case 003:
        // MnIXP - enable interface cross patch ...
        sim_debug(IMP_DBG_IOT,PDEVICE(line),"enable interface cross patch (PC=%06o)\n", PC-1);
        PMIDB(line)->iloop = TRUE;  PMIDB(line)->lloop = FALSE;  
        udp_set_link_loopback (PDEVICE(line), PMIDB(line)->link, FALSE);
        return dat;
      case 004:
        // MnIN - start modem input ...
        mi_debug_mio(line, PDIB(line)->rxdmc, "input");
        mi_start_rx(line);  return dat;
    }

  // SKS (skip) tests various modem conditions ...
  } else if (inst == ioSKS) {
    switch (fnc) {
      case 004:
        // MnERR - skip on modem error ...
        sim_debug(IMP_DBG_IOT,PDEVICE(line),"skip on error (PC=%06o, %s)\n",
          PC-1, PMIDB(line)->rxerror ? "SKIP" : "NOSKIP");
        return  PMIDB(line)->rxerror ? IOSKIP(dat) : dat;
      //   NOTE - the following skip, MnRXDONE, isn't actually part of the
      // original IMP design.  As far as I can tell the IMP had no way to
      // explicitly poll this flags; the only way to tell when a modem finished
      // was to catch the associated interrupt.  I've added one for testing
      // purposes, using an unimplemented SKS instruction.
      case 002:
        // MnRXDONE - skip on receive done ...
        return PMIDB(line)->rxpending ? dat : IOSKIP(dat);
    }
  }

  // Anything else is an error...
  sim_debug(IMP_DBG_WARN,PDEVICE(line),"UNIMPLEMENTED I/O (PC=%06o, instruction=%o, function=%02o)\n", PC-1, inst, fnc);
  return IOBADFNC(dat);
}



////////////////////////////////////////////////////////////////////////////////
///////////////////   H O S T   E V E N T   S E R V I C E   ////////////////////
////////////////////////////////////////////////////////////////////////////////


// Receiver service ...
t_stat mi_rx_service (UNIT *uptr)
{
  //   This is the standard simh "service" routine that's called when an event
  // queue entry expires.  It just polls the receiver and reschedules itself.
  // That's it!
  uint16 line = uptr->mline;
  mi_poll_rx(line);
  sim_activate(uptr, uptr->wait);
  return SCPE_OK;
}

// Transmitter service ...
t_stat mi_tx_service (uint32 quantum)
{
  //   This is the special transmitter service routine that's called by the RTC
  // service every time the RTC is updated.  This routine polls ALL the modem
  // transmitters (or at least any which are active) and figures out whether it
  // is time for an interrupt.
  uint32 i;
  for (i = 1;  i <= MI_NUM;  ++i) mi_poll_tx(i, quantum);
  return SCPE_OK;
}



////////////////////////////////////////////////////////////////////////////////
///////////////   D E V I C E   A C T I O N   C O M M A N D S   ////////////////
////////////////////////////////////////////////////////////////////////////////

// Reset device ...
t_stat mi_reset (DEVICE *dptr)
{
  // simh calls this routine for the RESET command ...
  UNIT *uptr = dptr->units;
  uint16 line = uptr->mline;

  // Reset the devices AND clear the interrupt enable bits ...
  mi_reset_rx(line);  mi_reset_tx(line);

  //   If the unit is attached, then make sure we restart polling because some
  // simh commands (e.g. boot) dump the pending event queue!
  sim_cancel(uptr);
  if ((uptr->flags & UNIT_ATT) != 0) sim_activate(uptr, uptr->wait);
  return SCPE_OK;
}

// Attach device ...
t_stat mi_attach (UNIT *uptr, CONST char *cptr)
{
  //   simh calls this routine for (what else?) the ATTACH command.  There are
  // three distinct formats for ATTACH -
  //
  //    ATTACH -p MIn COMnn          - attach MIn to a physical COM port
  //    ATTACH MIn llll:w.x.y.z:rrrr - connect via UDP to a remote simh host
  //
  t_stat ret;  char *pfn;  uint16 line = uptr->mline;
  t_bool fport = sim_switches & SWMASK('P');

  // If we're already attached, then detach ...
  if ((uptr->flags & UNIT_ATT) != 0) detach_unit(uptr);

  // The physical (COM port) attach isn't implemented yet ...
  if (fport)
    return sim_messagef(SCPE_ARG,"MI%d - physical COM support is not yet implemented\n", line);

  //   Make a copy of the "file name" argument.  udp_create() actually modifies
  // the string buffer we give it, so we make a copy now so we'll have something
  // to display in the "SHOW MIn ..." command.
  pfn = (char *) calloc (CBUFSIZE, sizeof (char));
  if (pfn == NULL) return SCPE_MEM;
  strncpy (pfn, cptr, CBUFSIZE);

  // Create the UDP connection.
  ret = udp_create(PDEVICE(line), cptr, &(PMIDB(line)->link));
  if (ret != SCPE_OK) {free(pfn);  return ret;};

  // Reset the flags and start polling ...
  uptr->flags |= UNIT_ATT;  uptr->filename = pfn;
  return mi_reset(find_dev_from_unit(uptr));
}

// Detach device ...
t_stat mi_detach (UNIT *uptr)
{
  //   simh calls this routine for (you guessed it!) the DETACH command.  This
  // disconnects the modem from any UDP connection or COM port and effectively
  // makes the modem "off line".  A disconnected modem acts like a real modem
  // with its phone line unplugged.
  t_stat ret;  uint16 line = uptr->mline;
  if ((uptr->flags & UNIT_ATT) == 0) return SCPE_OK;
  ret = udp_release(PDEVICE(line), PMIDB(line)->link);
  if (ret != SCPE_OK) return ret;
  PMIDB(line)->link = NOLINK;  uptr->flags &= ~UNIT_ATT;
  free (uptr->filename);  uptr->filename = NULL;
  return mi_reset(PDEVICE(line));
}

t_stat mi_set_loopback (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  t_stat ret = SCPE_OK; uint16 line = uptr->mline;

  switch (val) {
    case 0:     // LOOPINTERFACE
    case 1:     // NOLOOPINTERFACE
      PMIDB(line)->iloop = (val == 0) ? 1 : 0;
      break;
    case 2:     // LOOPLINE
    case 3:     // NOLOOPLINE
      if (PMIDB(line)->link == NOLINK)
        return SCPE_UNATT;
      val = (val == 2) ? 1 : 0;
      ret = udp_set_link_loopback (PDEVICE(line), PMIDB(line)->link, val);
      if (ret == SCPE_OK)
        PMIDB(line)->lloop = val;
      break;
    }
  return ret;
}

t_stat mi_show_loopback (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  uint16 line = uptr->mline;

  if (PMIDB(line)->iloop)
    fprintf (st, "Interface (local) Loopback");
  if (PMIDB(line)->lloop)
    fprintf (st, "Line (remote) Loopback");
  return SCPE_OK;
}

#endif // #ifdef VM_IMPTIP from the very top
