/* h316_imp.c- BBN ARPAnet IMP/TIP Specific Hardware
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

   tks          task switch device
   mlc          multiline controller (aka TIP)

   21-May-13    RLA     New file

   OVERVIEW

   This module implements the IMP pseudo device - this hack takes care of two
   custom devices in the IMP hardware - device 041, which implements task 
   switching and the RDIMPN instruction, and device 42, which implements the
   AMIMLC ("am I a multiline controller") instruction. This module also contains
   a few miscellaneous routines which are used by the IMP support in general.  

   IMP state is maintained in a set of state variables:

        MLC             always zero (TIP flag)
        IEN             task interrupt enabled
        IRQ             task interrupt pending

   TODO
*/
#ifdef VM_IMPTIP
#include "h316_defs.h"                  // H316 emulator definitions
#include "h316_imp.h"                   // ARPAnet IMP/TIP definitions

// Locals ...
uint16 imp_station  = IMP_STATION;      // IMP number (or address)
uint16 imp_ismlc    = 0;                // 1 for MLC (not yet implemented!)

// Externals from other parts of simh ...
extern uint16 dev_ext_int, dev_ext_enb; // current IRQ and IEN bit vectors
extern int32 PC;                        // current PC (for debug messages)
extern int32 stop_inst;                 // needed by IOBADFNC()

// Forward declarations ...
int32  imp_io      (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat imp_service (UNIT *uptr);
t_stat imp_reset   (DEVICE *dptr);
t_stat imp_show_station (FILE *st, UNIT *uptr, int32 val, CONST void *dp);
t_stat io_show_int (FILE *st, UNIT *uptr, int32 val, CONST void *dp);
t_stat imp_set_station (UNIT *uptr, int32 val, CONST char *cptr, void *dp);
t_stat io_set_int (UNIT *uptr, int32 val, CONST char *cptr, void *dp);



////////////////////////////////////////////////////////////////////////////////
//////////////////////   D A T A    S T R U C T U R E S   //////////////////////
////////////////////////////////////////////////////////////////////////////////

// IMP device information block ...
DIB imp_dib = { IMP, 2, IOBUS, IOBUS, INT_V_TASK, INT_V_NONE, &imp_io, 0 };

// IMP unit data (we have only one!) ...
UNIT imp_unit = { UDATA (&imp_service, 0, 0) };

// IMP device registers (for "EXAMINE IMP STATE") ...
REG imp_reg[] = {
  { FLDATA (MLC, imp_ismlc, 0), REG_RO },
  { FLDATA (IEN, dev_ext_enb, INT_V_TASK-INT_V_EXTD) },
  { FLDATA (IRQ, dev_ext_int, INT_V_TASK-INT_V_EXTD) },
  { NULL }
};

// IMP device modifiers (for "SET/SHOW IMP xxx") ...
MTAB imp_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "NUM", "NUM", &imp_set_station, &imp_show_station, NULL },
  { 0 }
};

// IMP debugging flags (for "SET IMP DEBUG=xxx") ...
DEBTAB imp_debug[] = {
  {"WARN",  IMP_DBG_WARN},
  {"IO",    IMP_DBG_IOT},
  {0}
};

// And finally tie it all together ...
DEVICE imp_dev = {
  "IMP", &imp_unit, imp_reg, imp_mod,
  1, 0, 0, 0, 0, 0,
  NULL, NULL, &imp_reset, NULL, NULL, NULL,
  &imp_dib, DEV_DIS|DEV_DISABLE|DEV_DEBUG, 0, imp_debug, NULL, NULL
};



////////////////////////////////////////////////////////////////////////////////
//////////   I M P   I / O   A N D   S E R V I C E   R O U T I N E S  //////////
////////////////////////////////////////////////////////////////////////////////

// Set and clear the TASK IRQ and IEN ...
#define SET_TASK_IRQ()  SET_EXT_INT((1u << (imp_dib.inum - INT_V_EXTD)))
#define CLR_TASK_IRQ()  CLR_EXT_INT((1u << (imp_dib.inum - INT_V_EXTD)))
#define CLR_TASK_IEN()  CLR_EXT_ENB((1u << (imp_dib.inum - INT_V_EXTD)))

// IMP I/O routine ...
int32 imp_io (int32 inst, int32 fnc, int32 dat, int32 dev)
{
  if (dev == IMP) {
    if ((inst == ioOCP) && (fnc == 000)) {
      // TASK - just set the task interrupt request bit ...
      sim_debug(IMP_DBG_IOT, &imp_dev, "request task interrupt (PC=%06o)\n", PC-1);
      SET_TASK_IRQ();  return dat;
    } else if ((inst == ioINA) && ((fnc == 010) || (fnc == 000))) {
      // RDIMPN - return the IMP address and always skip ...
      sim_debug(IMP_DBG_IOT, &imp_dev, "read address (PC=%06o)\n", PC-1);
      return IOSKIP(imp_station);
    }
  } else if (dev == IMP+1) {
    if ((inst == ioSKS) && (fnc == 000)) {
      // AMIMLC - skip if this machine is an MLC ...
      sim_debug(IMP_DBG_IOT, &imp_dev, "skip on MLC (PC=%06o %s)\n", PC-1, imp_ismlc ? "SKIP" : "NOSKIP");
      if (imp_ismlc != 0) return IOSKIP(dat);  else return dat;
    }
  }

  // Anything else is an error...
  sim_debug(IMP_DBG_WARN, &imp_dev, "UNIMPLEMENTED I/O (PC=%06o, instruction=%o, function=%02o)\n", PC-1, inst, fnc);
  return IOBADFNC(dat);
}

// Unit service ...
t_stat imp_service (UNIT *uptr)
{
  return SCPE_OK;
}



////////////////////////////////////////////////////////////////////////////////
///////////////   D E V I C E   A C T I O N   C O M M A N D S   ////////////////
////////////////////////////////////////////////////////////////////////////////

// Reset routine ...
t_stat imp_reset (DEVICE *dptr)
{
  // The simh RESET command clears both the interrupt request and enable...
  CLR_TASK_IRQ();  CLR_TASK_IEN();
  return SCPE_OK;
}



////////////////////////////////////////////////////////////////////////////////
/////////   D E V I C E   S E T   A N D   S H O W   C O M M A N D S   //////////
////////////////////////////////////////////////////////////////////////////////

// Show the station number ...
t_stat imp_show_station (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  fprintf(st,"station=%d", imp_station);
  return SCPE_OK;
}

// Set the station number ...
t_stat imp_set_station (UNIT *uptr, int32 val, CONST char *cptr, void *dp)
{
  uint32 newnum;  t_stat sts;
  if (cptr == NULL) return SCPE_ARG;
  newnum = get_uint (cptr, 10, 9999, &sts);
  if (newnum == 0) return SCPE_ARG;
  imp_station = newnum;
  return SCPE_OK;
}

#endif // #ifdef VM_IMPTIP from the very top
