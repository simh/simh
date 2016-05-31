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
t_stat hi_service (UNIT *uptr);
t_stat hi_reset (DEVICE *dptr);
t_stat hi_attach (UNIT *uptr, CONST char *cptr);
t_stat hi_detach (UNIT *uptr);



////////////////////////////////////////////////////////////////////////////////
//////////////////////   D A T A    S T R U C T U R E S   //////////////////////
////////////////////////////////////////////////////////////////////////////////

// Host interface data blocks ...
//   The HIDB is our own internal data structure for each host.  It keeps data
// about the TCP/IP connection, buffers, etc.
#define HI_HIDB(N)  {0, 0, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE}
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
#define HI_UNIT(N) {UDATA (&hi_service, UNIT_ATTABLE, 0), HI_POLL_DELAY, N, 0, 0, 0}
UNIT hi1_unit = HI_UNIT(1), hi2_unit = HI_UNIT(2);
UNIT hi3_unit = HI_UNIT(3), hi4_unit = HI_UNIT(4);

// Host Device Registers ...
//   These are the simh device "registers" - they c can be viewed with the
// "EXAMINE HIxn STATE" command and modified by "DEPOSIT HIxn ..."
#define HI_REG(N) {                                                     \
  { DRDATA (POLL, hi##N##_unit.wait,  24), REG_NZ + PV_LEFT },          \
  { FLDATA (RXIRQ, dev_ext_int, INT_V_HI##N##RX-INT_V_EXTD) },          \
  { FLDATA (RXIEN, dev_ext_enb, INT_V_HI##N##RX-INT_V_EXTD) },          \
  { DRDATA (RXTOT, hi##N##_db.rxtotal,32), REG_RO + PV_LEFT },          \
  { FLDATA (TXIRQ, dev_ext_int, INT_V_HI##N##TX-INT_V_EXTD) },          \
  { FLDATA (TXIEN, dev_ext_enb, INT_V_HI##N##TX-INT_V_EXTD) },          \
  { DRDATA (TXTOT, hi##N##_db.txtotal,32), REG_RO + PV_LEFT },          \
  { FLDATA (LLOOP, hi##N##_db.lloop,   0),          PV_RZRO },          \
  { FLDATA (ERROR, hi##N##_db.error,   0),          PV_RZRO },          \
  { FLDATA (READY, hi##N##_db.ready,   0),          PV_RZRO },          \
  { FLDATA (FULL,  hi##N##_db.full ,   0),          PV_RZRO },          \
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
  PHIDB(host)->lloop = PHIDB(host)->error = PHIDB(host)->enabled = FALSE;
  PHIDB(host)->ready = PHIDB(host)->eom = FALSE;
  PHIDB(host)->rxtotal = 0;
  CLR_RX_IRQ(host);  CLR_RX_IEN(host);
}

// Reset transmitter (clear flags AND initialize all data) ...
void hi_reset_tx (uint16 host)
{
  PHIDB(host)->lloop = PHIDB(host)->enabled = PHIDB(host)->full = FALSE;
  PHIDB(host)->txtotal = 0;
  CLR_TX_IRQ(host);  CLR_TX_IEN(host);
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
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "start regular output (PC=%06o)\n", PC-1);
        return dat;
      case 001:
        // HnIN - start host input ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "start input (PC=%06o)\n", PC-1);
        return dat;
      case 002:
        // HnFOUT - start final host output ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "start final output (PC=%06o)\n", PC-1);
        return dat;
      case 003:
        // HnXP - cross patch ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "enable cross patch (PC=%06o)\n", PC-1);
        return dat;
      case 004:
        // HnUNXP - un-cross patch ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "disable cross patch (PC=%06o)\n", PC-1);
        return dat;
      case 005:
        // HnENAB - enable ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "enable host (PC=%06o)\n", PC-1);
        return dat;
    }

  // SKS (skip) tests various modem conditions ...
  } else if (inst == ioSKS) {
    switch (fnc) {
      case 000:
        // HnERR - skip on host error ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "skip on error (PC=%06o %s)\n", PC-1, "NOSKIP");
        return dat;
      case 001:
        // HnRDY - skip on host ready ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "skip on ready (PC=%06o %s)\n", PC-1, "NOSKIP");
        return dat;
      case 002:
        // HnEOM - skip on end of message ...
        sim_debug(IMP_DBG_IOT, PDEVICE(host), "skip on end of message (PC=%06o %s)\n", PC-1, "NOSKIP");
        return dat;
      case 005:
        // HnFULL - skip on host buffer full ...
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

// Unit service ...
t_stat hi_service (UNIT *uptr)
{
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
  return SCPE_OK;
}

// Attach (connect) ...
t_stat hi_attach (UNIT *uptr, CONST char *cptr)
{
  // simh calls this routine for (what else?) the ATTACH command.
  uint16 host = uptr->hline;
  fprintf(stderr,"HI%d - host interface not yet implemented\n", host);
  return SCPE_IERR;
}

// Detach (connect) ...
t_stat hi_detach (UNIT *uptr)
{
  // simh calls this routine for (you guessed it!) the DETACH command.
  uint16 host = uptr->hline;
  fprintf(stderr,"HI%d - host interface not yet implemented\n", host);
  return SCPE_IERR;
}


#endif // #ifdef VM_IMPTIP from the very top
