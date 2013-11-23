/* h316_rtc.c- BBN ARPAnet IMP/TIP Real Time Clock and Watch Dog Timer
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

   rtc          real time clock
   wdt          watch dog timer

   21-May-13    RLA     New file

   OVERVIEW

   The IMP and TIP used a custom real time clock that was apparently created
   by BBN just for those devices.  The IMP/TIP RTC is NOT the same as the
   official Honeywell real time clock option, H316-12.  When emulating an IMP
   or TIP, this RTC device must be enabled and the standard simh H316 CLK
   device must be disabled.  

   The IMP and TIP also had a watch dog timer which, if ever allowed to time
   out, would cause a non-maskable interrupt via location 62(8) - this is the
   same trap location used by the memory lockout option, which the IMP/TIP
   lacked.  Not much is known about the WDT, and the current implementation is
   simply a place holder that doesn't do much.

   RTC state is maintained in a set of state variables:

        ENA             RTC is enabled
        COUNT           current count
        IEN             RTC interrupt enabled
        IRQ             RTC interrupt pending
        TPS             effective ticks per second
        WAIT            simulator time until the next tick

   WDT state is maintained in a set of state variables:

        COUNT           current countdown
        TMO             WDT timed out
        LIGHTS          last "set status lights"
        WAIT            simulator time until the next tick

   TODO

   Implement the WDT!!
*/
#ifdef VM_IMPTIP
#include "h316_defs.h"                  // H316 emulator definitions
#include "h316_imp.h"                   // ARPAnet IMP/TIP definitions

// Locals ...
uint32 rtc_interval = RTC_INTERVAL;     // RTC tick interval (in microseconds)
uint32 rtc_quantum  = RTC_QUANTUM;      // RTC update interval (in ticks)
uint32 rtc_tps      = 1000000UL / (RTC_INTERVAL * RTC_QUANTUM);
uint16 rtc_enabled  = 1;                // RTC enabled
uint32 rtc_count    = 0;                // current RTC count
uint32 wdt_delay    = WDT_DELAY;        // WDT timeout (in milliseconds, 0=none)
uint32 wdt_count    = 0;                // current WDT countdown
uint16 wdt_lights   = 0;                // last "set status lights" output

// Externals from other parts of simh ...
extern uint16 dev_ext_int, dev_ext_enb; // current IRQ and IEN bit vectors
extern int32 PC;                        // current PC (for debug messages)
extern int32 stop_inst;                 // needed by IOBADFNC()

// Forward declarations ...
int32  rtc_io      (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat rtc_service (UNIT *uptr);
t_stat rtc_reset   (DEVICE *dptr);
int32  wdt_io      (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat wdt_service (UNIT *uptr);
t_stat wdt_reset   (DEVICE *dptr);
t_stat rtc_set_interval (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rtc_show_interval (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat rtc_set_quantum(UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat rtc_show_quantum (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat wdt_set_delay (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat wdt_show_delay (FILE *st, UNIT *uptr, int32 val, void *desc);



////////////////////////////////////////////////////////////////////////////////
//////////////////   R T C   D A T A    S T R U C T U R E S   //////////////////
////////////////////////////////////////////////////////////////////////////////

// RTC device information block ...
DIB rtc_dib = { RTC, 1, IOBUS, IOBUS, INT_V_RTC, INT_V_NONE, &rtc_io, 0 };

// RTC unit data block (we have only one unit!) ...
UNIT rtc_unit = { UDATA (&rtc_service, 0, 0), (RTC_INTERVAL*RTC_QUANTUM) };

// RTC device registers (for "EXAMINE RTC STATE") ...
REG rtc_reg[] = {
  { FLDATA (ENA, rtc_enabled, 0) },
  { DRDATA (COUNT, rtc_count, 16), PV_LEFT },
  { FLDATA (IEN, dev_ext_enb, INT_V_RTC-INT_V_EXTD) },
  { FLDATA (IRQ, dev_ext_int, INT_V_RTC-INT_V_EXTD) },
  { DRDATA (TPS, rtc_tps, 32), PV_LEFT },
  { DRDATA (WAIT, rtc_unit.wait, 24), REG_NZ + PV_LEFT },
  { NULL }
};

// RTC device modifiers (for "SET/SHOW RTC xxx") ...
MTAB rtc_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "INTERVAL", "INTERVAL", &rtc_set_interval, &rtc_show_interval, NULL },
  { MTAB_XTD|MTAB_VDV, 0, "QUANTUM",   "QUANTUM", &rtc_set_quantum,   &rtc_show_quantum, NULL },
  { 0 }
};

// RTC debugging flags (for "SET RTC DEBUG=xxx") ...
DEBTAB rtc_debug[] = {
  {"WARN",  IMP_DBG_WARN},
  {"IO",    IMP_DBG_IOT},
  {0}
};

// And finally tie it all together ...
DEVICE rtc_dev = {
  "RTC", &rtc_unit, rtc_reg, rtc_mod,
  1, 0, 0, 0, 0, 0,
  NULL, NULL, &rtc_reset, NULL, NULL, NULL,
  &rtc_dib, DEV_DIS|DEV_DISABLE|DEV_DEBUG, 0, rtc_debug, NULL, NULL
};



////////////////////////////////////////////////////////////////////////////////
//////////////////   W D T   D A T A    S T R U C T U R E S   //////////////////
////////////////////////////////////////////////////////////////////////////////

// WDT device information block ...
DIB wdt_dib = { WDT, 1, IOBUS, IOBUS, INT_V_NONE, INT_V_NONE, &wdt_io, 0 };

// WDT unit data block (it has only one) ...
UNIT wdt_unit = { UDATA (&wdt_service, 0, 0), 1000 };

// WDT device registers (for "EXAMINE WDT STATE") ...
REG wdt_reg[] = {
  { DRDATA (COUNT, wdt_count, 16), PV_LEFT },
  { DRDATA (WAIT, wdt_unit.wait, 24), REG_NZ | PV_LEFT },
  { ORDATA (LIGHTS, wdt_lights, 16), REG_RO | PV_LEFT },
  { NULL }
};

// WDT device modifiers (for "SET/SHOW WDT xxx") ...
MTAB wdt_mod[] = {
  { MTAB_XTD | MTAB_VDV, 0, "DELAY", "DELAY", &wdt_set_delay, &wdt_show_delay, NULL },
  { 0 }
};

// WDT debugging flags (for "SET WDT DEBUG=xxx") ...
DEBTAB wdt_debug[] = {
  {"WARN",   IMP_DBG_WARN},
  {"IO",     IMP_DBG_IOT},
  {"LIGHTS", WDT_DBG_LIGHTS},
  {0}
};

// And summarize ...
DEVICE wdt_dev = {
  "WDT", &wdt_unit, wdt_reg, wdt_mod,
  1, 0, 0, 0, 0, 0,
  NULL, NULL, &wdt_reset, NULL, NULL, NULL,
  &wdt_dib, DEV_DIS|DEV_DISABLE|DEV_DEBUG, 0, wdt_debug, NULL, NULL
};



////////////////////////////////////////////////////////////////////////////////
//////////   R T C   I / O   A N D   S E R V I C E   R O U T I N E S  //////////
////////////////////////////////////////////////////////////////////////////////

// Set and clear the RTC IRQ and IEN ...
#define SET_RTC_IRQ()  SET_EXT_INT((1u << (rtc_dib.inum - INT_V_EXTD)))
#define CLR_RTC_IRQ()  CLR_EXT_INT((1u << (rtc_dib.inum - INT_V_EXTD)))
#define CLR_RTC_IEN()  CLR_EXT_ENB((1u << (rtc_dib.inum - INT_V_EXTD)))

// RTC IO routine ...
int32 rtc_io (int32 inst, int32 fnc, int32 dat, int32 dev)
{
  switch (inst) {
    case ioOCP:
      if (fnc == 010) {
        // CLKOFF - turn the RTC off ...
        sim_cancel(&rtc_unit);  rtc_enabled = 0;  CLR_RTC_IRQ();
        sim_debug(IMP_DBG_IOT, &rtc_dev, "disabled (PC=%06o)\n", PC-1);
        return dat;
      } else if (fnc == 000) {
        // CLKON - turn the RTC on ...
        rtc_enabled = 1;  CLR_RTC_IRQ();
        if (sim_is_active(&rtc_unit) == 0)
          sim_activate (&rtc_unit, sim_rtc_init (rtc_unit.wait));
        sim_debug(IMP_DBG_IOT, &rtc_dev, "enabled (PC=%06o)\n", PC-1);
        return dat;
      }
      break;

    case ioINA:
      if ((fnc == 010) || (fnc == 000)) {
        // RDCLOK - return the current count
        sim_debug(IMP_DBG_IOT, &rtc_dev, "read clock (PC=%06o, RTC=%06o)\n", PC-1, (rtc_count & DMASK));
        return IOSKIP((rtc_count & DMASK));
      }
      break;
  }

  sim_debug(IMP_DBG_WARN, &rtc_dev, "UNIMPLEMENTED I/O (PC=%06o, instruction=%o, function=%02o)\n", PC-1, inst, fnc);
  return IOBADFNC(dat);
}

// RTC unit service ...
t_stat rtc_service (UNIT *uptr)
{
  //   Add the current quantum to the clock register and, if the clock register
  // has overflowed, request an interrupt.  The real hardware interrupts when
  // there is a carry out of the low byte (in other words, every 256 clocks).
  // Note that we can't simply check the low byte for zero to detect overflows
  // because of the quantum.  Since we aren't necessarily incrementing by 1, we
  // may never see a value of exactly zero.  We'll have to be more clever.
  uint8 rtc_high = HIBYTE(rtc_count);
  rtc_count = (rtc_count + rtc_quantum) & DMASK;
  if (HIBYTE(rtc_count) != rtc_high) {
    sim_debug(IMP_DBG_IOT, &rtc_dev, "interrupt request\n");
    SET_RTC_IRQ();
  }
  mi_tx_service(rtc_quantum);
  uptr->wait = sim_rtc_calb (rtc_tps);                  /* recalibrate */
  sim_activate_after (uptr, 1000000/rtc_tps);           /* reactivate unit */
  return SCPE_OK;
}



////////////////////////////////////////////////////////////////////////////////
//////////   W D T   I / O   A N D   S E R V I C E   R O U T I N E S  //////////
////////////////////////////////////////////////////////////////////////////////

// WDT IO routine ...
int32 wdt_io (int32 inst, int32 fnc, int32 dat, int32 dev)
{
  if ((inst == ioOCP) && (fnc == 0)) {
    // Reset WDT ...
    sim_debug(IMP_DBG_IOT, &wdt_dev, "reset (PC=%06o)\n", PC-1);
    return dat;
  } else if ((inst == ioOTA) && (fnc == 0)) {
    // Set status lights ...
      if (wdt_lights != dat) {
        sim_debug(WDT_DBG_LIGHTS, &wdt_dev, "changed to %06o\n", dat);
      }
    sim_debug(IMP_DBG_IOT, &wdt_dev, "set status lights (PC=%06o, LIGHTS=%06o)\n", PC-1, dat);
    wdt_lights = dat;  return dat;
  }

  sim_debug(IMP_DBG_WARN, &wdt_dev, "UNIMPLEMENTED I/O (PC=%06o, instruction=%o, function=%02o)\n", PC-1, inst, fnc);
  return IOBADFNC(dat);
}

// WDT unit service ...
t_stat wdt_service (UNIT *uptr)
{
  return SCPE_OK;
}



////////////////////////////////////////////////////////////////////////////////
///////////////   D E V I C E   A C T I O N   C O M M A N D S   ////////////////
////////////////////////////////////////////////////////////////////////////////

// RTC reset routine ...
t_stat rtc_reset (DEVICE *dptr)
{
  //   Clear the interrupt enable and any pending interrupts, reset the count
  // and enable the clock.  At least I assume that's what a reset does - the
  // docs aren't too specific on this point...
  rtc_enabled = 1;  rtc_count = 0;
  CLR_RTC_IRQ();  CLR_RTC_IEN();
  sim_cancel (&rtc_unit);
  sim_register_clock_unit ((dptr->flags & DEV_DIS) ? NULL : &rtc_unit);
  return SCPE_OK;
}

// WDT reset routine ...
t_stat wdt_reset (DEVICE *dptr)
{
  // Clear the WDT countdown and turn off all the lights ...
  wdt_count = 0;  wdt_lights = 0;
  sim_cancel (&wdt_unit);
  return SCPE_OK;
}



////////////////////////////////////////////////////////////////////////////////
/////////   D E V I C E   S E T   A N D   S H O W   C O M M A N D S   //////////
////////////////////////////////////////////////////////////////////////////////

// Set/Show RTC interval ...
t_stat rtc_set_interval (UNIT *uptr, int32 val, char *cptr, void *desc)
{
  uint32 newint, newtps;  t_stat ret;
  if (cptr == NULL) return SCPE_ARG;
  newint = get_uint (cptr, 10, 1000000, &ret);
  if (ret != SCPE_OK) return ret;
  if (newint == 0) return SCPE_ARG;
  newtps = 1000000UL / (newint * rtc_quantum);
  if ((newtps == 0) || (newtps >= 100000)) return SCPE_ARG;
  rtc_interval = newint;  rtc_tps = newtps;
  uptr->wait = sim_rtc_calb (rtc_tps);
  return SCPE_OK;
}

t_stat rtc_show_interval (FILE *st, UNIT *uptr, int32 val, void *desc)
{
  fprintf(st,"interval=%d (us)", rtc_interval);
  return SCPE_OK;
}

// Set/Show RTC quantum ...
t_stat rtc_set_quantum (UNIT *uptr, int32 val, char *cptr, void *desc)
{
  uint32 newquant, newtps;  t_stat ret;
  if (cptr == NULL) return SCPE_ARG;
  newquant = get_uint (cptr, 10, 1000000, &ret);
  if (ret != SCPE_OK) return ret;
  if (newquant == 0) return SCPE_ARG;
  newtps = 1000000UL / (rtc_interval * newquant);
  if ((newtps == 0) || (newtps >= 100000)) return SCPE_ARG;
  rtc_quantum = newquant;  rtc_tps = newtps;
  uptr->wait = sim_rtc_calb (rtc_tps);
  return SCPE_OK;
}

t_stat rtc_show_quantum (FILE *st, UNIT *uptr, int32 val, void *desc)
{
  fprintf(st,"quantum=%d (ticks)", rtc_quantum);
  return SCPE_OK;
}

// Set/Show WDT delay ...
t_stat wdt_set_delay (UNIT *uptr, int32 val, char *cptr, void *desc)
{
  uint32 newint;  t_stat ret;
  if (cptr == NULL) return SCPE_ARG;
  newint = get_uint (cptr, 10, 65535, &ret);
  if (ret != SCPE_OK) return ret;
  if (newint != 0) {
    fprintf(stderr,"WDT - timeout not yet implemented\n");
    return SCPE_IERR;
  }
  wdt_delay = newint;
// TBA add calculations here???
  return SCPE_OK;
}

t_stat wdt_show_delay (FILE *st, UNIT *uptr, int32 val, void *desc)
{
  if (wdt_delay > 0)
    fprintf(st,"delay=%d (ms)", wdt_delay);
  else
    fprintf(st,"no timeout");
  return SCPE_OK;
}

#endif // #ifdef VM_IMPTIP from the very top
