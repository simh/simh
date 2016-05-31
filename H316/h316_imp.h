/* h316_imp.h- BBN ARPAnet IMP/TIP Definitions

   Copyright (c) 2013, Robert Armstrong, bob@jfcl.com

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

   21-May-13    RLA     New file.
*/
#ifdef VM_IMPTIP
#ifndef H316_IMP_H_
#define H316_IMP_H_   0
#include "sim_defs.h"

// Common modem and host parameters ...
#define MI_NUM            5     // number of modem interfaces
#define HI_NUM            4     // number of host interfaces
#define MI_MAX_MSG      256     // longest possible modem message (words!)
#define HI_MAX_MSG      256     // longest possible host message (words!)
#define MI_RXPOLL       100     // RX polling delay for UDP messages
#define MI_TXBPS      56000UL   // default TX speed (bits per second)
#define HI_POLL_DELAY  1000     // polling delay for messages

// Modem interface, line #1 ...
#define MI1                     071     // IO address for modem interface #1
#define MI1_RX_DMC       (DMC1-1+ 1)    // DMC channel for modem 1 receive
#define MI1_TX_DMC       (DMC1-1+ 6)    // DMC channel for modem 1 transmit
#define INT_V_MI1RX  (INT_V_EXTD+15)    // modem 1 receive interrupt
#define INT_V_MI1TX  (INT_V_EXTD+10)     // modem 1 transmit interrupt

// Modem interface, line #2 ...
#define MI2                     072     // IO address for modem interface #2
#define MI2_RX_DMC       (DMC1-1+ 2)    // DMC channel for modem 2 receive
#define MI2_TX_DMC       (DMC1-1+ 7)    // DMC channel for modem 2 transmit
#define INT_V_MI2RX  (INT_V_EXTD+14)    // modem 2 receive interrupt
#define INT_V_MI2TX  (INT_V_EXTD+ 9)    // modem 2 transmit interrupt

// Modem interface, line #3 ...
#define MI3                     073     // IO address for modem interface #3
#define MI3_RX_DMC       (DMC1-1+ 3)    // DMC channel for modem 3 receive
#define MI3_TX_DMC       (DMC1-1+ 8)    // DMC channel for modem 3 transmit
#define INT_V_MI3RX  (INT_V_EXTD+13)    // modem 3 receive interrupt
#define INT_V_MI3TX  (INT_V_EXTD+ 8)    // modem 3 transmit interrupt

// Modem interface, line #4 ...
#define MI4                     074     // IO address for modem interface #4
#define MI4_RX_DMC       (DMC1-1+ 4)    // DMC channel for modem 4 receive
#define MI4_TX_DMC       (DMC1-1+ 9)    // DMC channel for modem 4 transmit
#define INT_V_MI4RX  (INT_V_EXTD+12)    // modem 4 receive interrupt
#define INT_V_MI4TX  (INT_V_EXTD+ 7)    // modem 4 transmit interrupt

// Modem interface, line #5 ...
#define MI5                     075     // IO address for modem interface #5
#define MI5_RX_DMC       (DMC1-1+ 5)    // DMC channel for modem 5 receive
#define MI5_TX_DMC       (DMC1-1+10)    // DMC channel for modem 5 transmit
#define INT_V_MI5RX  (INT_V_EXTD+11)     // modem 5 receive interrupt
#define INT_V_MI5TX  (INT_V_EXTD+ 6)     // modem 5 transmit interrupt

// Host interface, line #1 ...
#define HI1                     070     // device address for host interface #1
#define HI1_RX_DMC       (DMC1+13-1)    // DMC channel for host 1 receive
#define HI1_TX_DMC       (DMC1+11-1)    // DMC channel for host 1 transmit
#define INT_V_HI1RX  (INT_V_EXTD+ 3)    // host 1 receive interrupt
#define INT_V_HI1TX  (INT_V_EXTD+ 5)    // host 1 transmit interrupt

// Host interface, line #2 ...
#define HI2                     060     // device address for host interface #2
#define HI2_RX_DMC       (DMC1-1+14)    // DMC channel for host 2 receive
#define HI2_TX_DMC       (DMC1-1+12)    // DMC channel for host 2 transmit
#define INT_V_HI2RX  (INT_V_EXTD+ 2)    // host 2 receive interrupt
#define INT_V_HI2TX  (INT_V_EXTD+ 4)    // host 2 transmit interrupt

// Host interface, line #3 ...
#define HI3                     051     // device address for host interface #3
#define HI3_RX_DMC        (DMC1-1+16)   // DMC channel for host 3 receive
#define HI3_TX_DMC        (DMC1-1+15)   // DMC channel for host 3 transmit
#define INT_V_HI3RX   (INT_V_EXTD+ 6)   // host 3 receive interrupt
#define INT_V_HI3TX   (INT_V_EXTD+11)   // host 3 transmit interrupt

// Host interface, line #4 ...
#define HI4                     050     // device address for host interface #4
#define HI4_RX_DMC       (DMC1-1+10)    // DMC channel for host 4 receive
#define HI4_TX_DMC       (DMC1-1+ 5)    // DMC channel for host 4 transmit
#define INT_V_HI4RX  (INT_V_EXTD+ 7)    // host 4 receive interrupt
#define INT_V_HI4TX  (INT_V_EXTD+12)    // host 4 transmit interrupt

// IMP defaults ...
#define IMP                     041     // IMP device IO address (41 & 42 actually!)
#define INT_V_TASK   (INT_V_EXTD+ 0)    // task switch interrupt number
#define IMP_STATION               1     // default station number

// RTC defaults ...
#define RTC                     040     // real time clock IO address
#define INT_V_RTC    (INT_V_EXTD+ 1)    // RTC interrupt number
#define RTC_INTERVAL            20UL    // default RTC interval (20us == 50kHz)
#define RTC_QUANTUM             32UL    // default RTC quantum (32 ticks)

// WDT defaults ...
#define WDT                     026     // watchdog timer IO address
#define WDT_VECTOR           000062     // WDT timeout vector
#define WDT_DELAY                 0     // default WDT timeout (in milliseconds)

// Debugging flags ...
//   In general, these bits are used as arguments for sim_debug().  Bits that
// begin with "IMP_DBG_xyz" are shared by more than one device (e.g. IMP_DBG_UDP)
// and must have unique bit assignments.  Bits prefixed with a device name (e.g.
// "MI_DBG_xyz") apply to that device only.
#define IMP_DBG_WARN    0x0001  // all: print warnings
#define IMP_DBG_IOT     0x0002  // all: trace all program I/O instructions
#define IMP_DBG_UDP     0x0004  // all: trace UDP packets
#define MI_DBG_MSG      0x8000  // modem: decode and print all messages
#define WDT_DBG_LIGHTS  0x8000  // wdt: show status light changes

// Synonyms for DIB and UNIT fields ...
#define rxdmc   chan            // dib->rxdmc
#define txdmc   chan2           // dib->txdmc
#define rxint   inum            // dib->rxint
#define txint   inum2           // dib->txint

// Modem interface data block ....
//   One of these is allocated to every modem interface to keep track of the
// current state, COM port, UDP connection , etc...
struct _MIDB {
  // Receiver data ...
  t_bool      rxpending;        // TRUE if a read is pending on this line
  t_bool      rxerror;          // TRUE if any modem error detected
  uint32      rxtotal;          // total number of H316 words received
  // Transmitter data ...
  uint32      txtotal;          // total number of H316 words transmitted
  uint32      txdelay;          // RTC ticks until TX done interrupt
  // Other data ...
  t_bool      lloop;            // line loop back enabled
  t_bool      iloop;            // interface loop back enabled
  int32       link;             // h316_udp link number
  uint32      bps;              // simulated line speed or COM port baud rate
};
typedef struct _MIDB MIDB;

// Host interface data block ...
//   One of these is allocated to every host interface ...
struct _HIDB {
  // Receiver (HOST -> IMP) data ...
  uint32      rxtotal;          // total host messages received
  // Transmitter (IMP -> HOST) data ...
  uint32      txtotal;          // total host messages sent
  // Other data ...
  t_bool      lloop;            // local loop back enabled
  t_bool      enabled;          // TRUE if the host is enabled
  t_bool      error;            // TRUE for any host error
  t_bool      ready;            // TRUE if the host is ready
  t_bool      full;             // TRUE if the host buffer is full
  t_bool      eom;              // TRUE when end of message is reached
};
typedef struct _HIDB HIDB;

// I can't believe Bob managed to live without these, but I can't!
#ifndef LOBYTE  // these are in winsock.h too!
#define LOBYTE(x)       ((uint8) ( (x)       & 0xFF))
#define HIBYTE(x)       ((uint8) (((x) >> 8) & 0xFF))
#define MKWORD(h,l)     ((uint16) ( (((h)&0xFF) << 8) | ((l)&0xFF) ))
#define LOWORD(x)       ((uint16) ( (x)        & 0xFFFF))
#define HIWORD(x)       ((uint16) (((x) >> 16) & 0xFFFF))
#define MKLONG(h,l)     ((uint32) ( (((h)&0xFFFF) << 16) | ((l)&0xFFFF) ))
#endif

// Prototypes for the RTC module ...
//   I really hate sharing things like this, but it's the only way to get the
// modem transmitter timing exactly right!
extern uint32 rtc_interval;
extern t_stat mi_tx_service (uint32 quantum);

// Prototypes for UDP modem/host interface emulation routines ...
#define NOLINK  (-1)
t_stat udp_create (DEVICE *pdtr, const char *premote, int32 *plink);
t_stat udp_release (DEVICE *dptr, int32 link);
t_stat udp_send (DEVICE *pdtr, int32 link, uint16 *pdata, uint16 count);
t_stat udp_set_link_loopback (DEVICE *dptr, int32 link, t_bool enable_loopback);
int32 udp_receive (DEVICE *dptr, int32 link, uint16 *pdata, uint16 maxbufg);

#endif  // #ifndef _H316_IMP_H_
#endif  // #ifdef VM_IMPTIP
