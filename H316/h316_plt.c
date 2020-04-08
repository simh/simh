/* h316_plt.c: Honeywell 316/516 incremental plotter

   Copyright (c) 2017, Adrian P. Wise

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
   ADRIAN P WISE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Adrian P Wise shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Adrian P Wise.

   plt          EOM 2111 to 2114 incremental plotter

   11-Feb-17    APW     initial work


   This models Incremental Plotter Options EOM 2111 to 2114.

   Each option consists of an unmodified Computer Instrumentation Ltd.
   Incremental Plotter and an interface to couple the plotter to a
   DDP-416, DDP-516, or H316 computer.

   EOM No. DESCRIPTION                                            DRAWING No.
   2111    Interface and CI Model 141 Plotter 0.005 in. step size 41275000-000
   2112    Interface and CI Model 142 Plotter 0.010 in. step size 41275001-000
   2113    Interface and CI Model 341 Plotter 0.1mm step size     41275002-000
   2114    Interface and CI Model 342 Plotter 0.2mm step size     41275003-000

   While this was a Honeywell-supported option, it isn't documented in
   the standard programmers' reference manual, so a brief description of
   how it is programmed is in order:

   This is an unusual output device in that it doesn't use the OTA
   instruction - all control is effected via OCP instructions which generally
   move the pen (relative to the paper) one increment in the direction of
   one of the eight points on a compass. OCP instructions are also provided
   to move the pen down (onto the paper) or up (off of the paper). 

   OCP '0127 Carriage right             East
   OCP '0227 Carriage left              West
   OCP '0427 Drum up                    North
   OCP '0527 Drum up, carriage right    North East 
   OCP '0627 Drum up, carriage left     North West
   OCP '1027 Drum down                  South 
   OCP '1127 Drum down, carriage right  South East 
   OCP '1227 Drum down, carriage left   South West 
   OCP '1427 Pen down
   OCP '1627 Pen up

   SKS '0127 Skip, if not busy
   SKS '0227 Skip, if not limit?
   SKS '0427 Skip, if not interrupting

   SMK '0020 Set Interrupt Mask (Bit 13)

   
   Output from the simulator

   For operation in simh the plotter, PLT, is attached to an output file into
   which either an ASCII or binary desription of the pen movements is written.
   This may the be post-processed to yield a postscript file which can then
   be printed or viewed on-screen on a modern machine.

   The ASCII file format is simply a series of codes (see the pd_names[] array
   below), one per line, denoting a compass-point direction or pen up/down
   command. This may optionally be followed by a decimal integer which is
   a repeat count (i.e. the number of *additional* steps above one).

   The binary file format yields much smaller files, and is the default.
   The file is a series of bytes. A byte with zero in the most significant bit
   is a command. The next four bits give the direction or pen movement, and
   are simply the function code (first two octal digits) of the OCP
   instructions, above. The least significant three bits of the byte give a
   repeat count for the command. Each command byte may be preceded by one
   or more "prefix" bytes which has a one in the most significant bit and the
   remainder carry seven more bits of the repeat count. (In a series of
   prefix bytes and a command byte, the more significant bits of the repeat
   count occur earliest, with the least significant 3 bits in the command
   byte.)
   

   Timebase
 
   The timebase isn't terribly well defined. I think the unit is an
   "instruction", but the time for a H316 instruction is highly variable.
 
   The basic "cycle time" of a H316 is 1.6us which is the time to
   complete both a read and a write of core memory. (Remember that reading
   core memory is destructive so every read is followed by a write to
   restore the contents.)
 
   Only the very simplest instructions are one "cycle", since this is only
   the time required to do an instruction fetch. Any operand access must
   take an additional "cycle", and several instructions are three cycles
   since they both read and write memory. To further complicate things
   iterative instructions (shift, multiply, divide) work in units of half
   a cycle per iteration (since they aren't tied to core cycles) and many
   "generic" instructions take 1.5 cycles.

   The verification-and-test program for the plotter reports the speed
   of the plotter in terms of memory cycles per increment. The number
   reported is the same as the hardware with a time period used in
   simh of 1673 +/- 6 and this implies a timebase unit of roughly 2.0 us.
 
   ASCII/Binary

   The handling of ASCII and binary files is copied directly from the
   standard devices code (h316_stddev.c) for consistency. In this case
   though there is no actual distinction between ASCII and Unix ASCII.

   Plotter option

   The option being use may be set with "SET PLT <option>" where the
   <option> is 2111, 2112, 2113, or 2114. The default option is 2113
   (largely because that happens to the be the physical hardware I
   actually own).

   Registers

   Most of the register as read-only and simply allow examination of the
   current state of the plotter:

   XPOS:     Current position in X dimension
   YPOS:     Current position in Y dimension
   BSY:      Whether the plotter is busy (i.e. mid-step)
   DIRN:     Current direction of travel
   COUNT:    How many steps taken in that direction
   PEN:      Whether the pen is down
   PHASE:    Internal state - essentially if we're producing output yet
   LIMIT:    Value of XPOS above which limit is reported
   ITIME:    Time period for an increment
   PTIME:    Time period to raise/lower pen
   INTREQ:   Whether interrupting
   ENABLE:   Whether interrupt enabled
   STOP_IOE: Whether to stop on an I/O error (default TRUE)

*/

#include "h316_defs.h"

extern int32 dev_int, dev_enb;
extern int32 stop_inst;

#define UNIT_V_ASC      (TTUF_V_UF + 0)                 /* ASCII */
#define UNIT_V_UASC     (TTUF_V_UF + 1)                 /* Unix ASCII */
#define UNIT_V_OPN0     (TTUF_V_UF + 2)                 /* Option Index */
#define UNIT_V_OPN1     (TTUF_V_UF + 3)                 /* Option Index */
#define UNIT_ASC        (1 << UNIT_V_ASC)
#define UNIT_UASC       (1 << UNIT_V_UASC)
#define UNIT_OPN0       (1 << UNIT_V_OPN0)
#define UNIT_OPN1       (1 << UNIT_V_OPN1)

#define INSTR_PER_SEC 501900 /* About 2.0us - gives timing that matches hardware */

#define DEFAULT_INCR_FREQ 300
#define DEFAULT_PEN_FREQ   50

#define PLT_INCR_WAIT (INSTR_PER_SEC / DEFAULT_INCR_FREQ)
#define PLT_PEN_WAIT  (INSTR_PER_SEC / DEFAULT_PEN_FREQ )

#define PLT_INITIAL_XPOS 42

enum option {
  op_2111 = 0,
  op_2112,
  op_2113,
  op_2114
};

#define DEFAULT_OPTION op_2113

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

struct PlotterModel {
  int metric;      /* Flag: Metric or imperial  */
  int step;        /* In 0.1mm or mil units     */
  int paper_width; /* In 0.1mm or mil units     */
  int limit_width; /* In 0.1mm or mil units     */
  int incr_freq;   /* Incr per second           */
  int pen_freq;    /* Pen operations per second */
};

static const struct PlotterModel plotter_models[4] = {
  {FALSE,  5, 14125, 13375, 300, 50}, /* Option 2111 */
  {FALSE, 10, 14125, 13375, 250, 50}, /* Option 2112 */
  {TRUE,   1,  3600,  3400, 300, 50}, /* Option 2113 */
  {TRUE,   2,  3600,  3400, 300, 50}  /* Option 2114 */
};

enum PHASE {
  LIMIT,    /* Waiting to reach an EW limit */
  UNLIMIT,  /* Waiting to come out of limit */
  SHOWTIME  /* Normal operation             */
};

enum PLT_DIRN {
  PD_NULL = 000,
  PD_N    = 004,
  PD_NE   = 005,
  PD_E    = 001,
  PD_SE   = 011,
  PD_S    = 010,
  PD_SW   = 012,
  PD_W    = 002,
  PD_NW   = 006,
  PD_UP   = 016,
  PD_DN   = 014,
  
  PD_NUM
};

static const char *pd_names[16] = {
  "(null)", "E",       "W",  "(error)",
  "N",      "NE",      "NW", "(error)",
  "S",      "SE",      "SW", "(error)",
  "DN",     "(error)", "UP", "(error)" 
};

static  int32        plt_xpos     = PLT_INITIAL_XPOS;
static  int32        plt_ypos     = 0;
static uint32        plt_busy     = FALSE;
static enum PLT_DIRN plt_dirn     = PD_NULL;
static uint32        plt_count    = 0;
static uint32        plt_pen      = FALSE;
static enum PHASE    plt_phase    = LIMIT;
static uint32        plt_xlimit   = 3400;
static uint32        plt_pen_wait = PLT_PEN_WAIT;
static uint32        plt_stopioe  = TRUE;

static int32  pltio (int32 inst, int32 fnc, int32 dat, int32 dev);
static t_stat plt_svc (UNIT *uptr);
static t_stat plt_reset (DEVICE *dptr);
static t_stat plt_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat plt_set_option (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat plt_attach (UNIT *uptr, CONST char *cptr);
static t_stat plt_detach (UNIT *uptr);

/* PLT data structures
   plt_dev      PLT device descriptor
   plt_unit     PLT unit descriptor
   plt_mod      PLT modifiers
   plt_reg      PLT register list
*/

static DIB plt_dib = { PLT, 1, IOBUS, IOBUS, INT_V_PLT, INT_V_NONE, &pltio, 0 };

#define MODE_MASK   (UNIT_ATTABLE+UNIT_ASC+UNIT_UASC)
#define MODE_BINARY (UNIT_ATTABLE)
#define MODE_ASCII  (UNIT_ATTABLE+UNIT_ASC)
#define MODE_UASCII (UNIT_ATTABLE+UNIT_ASC+UNIT_UASC)

#define OPN_MASK (UNIT_OPN0+UNIT_OPN1)
#define OPN_2111 0
#define OPN_2112 (UNIT_OPN0)
#define OPN_2113 (UNIT_OPN1)
#define OPN_2114 (UNIT_OPN0+UNIT_OPN1)

static UNIT plt_unit = { UDATA (&plt_svc,
                                UNIT_SEQ+UNIT_ATTABLE+OPN_2113, 0),
                         PLT_INCR_WAIT};

static REG plt_reg[] = {
  { DRDATA (XPOS,     plt_xpos,             32), PV_LEFT | REG_RO  },
  { DRDATA (YPOS,     plt_ypos,             32), PV_LEFT | REG_RO  },
  { FLDATA (BSY,      plt_busy,              0),           REG_RO  },
  { ORDATA (DIRN,     plt_dirn,              4),           REG_RO  },
  { DRDATA (COUNT,    plt_count,            32), PV_LEFT | REG_RO  },
  { FLDATA (PEN,      plt_pen,               0),           REG_RO  },
  { ORDATA (PHASE,    plt_phase,             2),           REG_HRO },
  { DRDATA (LIMIT,    plt_xlimit,           16), PV_LEFT | REG_RO  },
  { DRDATA (ITIME,    plt_unit.wait,        24), PV_LEFT           },
  { DRDATA (PTIME,    plt_pen_wait,         24), PV_LEFT           },
  { FLDATA (INTREQ,   dev_int,       INT_V_PLT)                    },
  { FLDATA (ENABLE,   dev_enb,       INT_V_PLT)                    },
  { FLDATA (STOP_IOE, plt_stopioe,           0)                    },
  { NULL }
};

MTAB plt_mod[] = {
  { MODE_MASK, MODE_BINARY,  NULL,                  "BINARY", &plt_set_mode   },
  { MODE_MASK, MODE_ASCII,   "ASCII",               "ASCII",  &plt_set_mode   },
  { MODE_MASK, MODE_UASCII,  "Unix ASCII",          "UASCII", &plt_set_mode   },
  { OPN_MASK, OPN_2111,      "2111 (0.005\" step)", "2111",   &plt_set_option },
  { OPN_MASK, OPN_2112,      "2112 (0.010\" step)", "2112",   &plt_set_option },
  { OPN_MASK, OPN_2113,      "2113 (0.1 mm step)",  "2113",   &plt_set_option },
  { OPN_MASK, OPN_2114,      "2114 (0.2 mm step)",  "2114",   &plt_set_option },
  { 0 }
};

DEVICE plt_dev = {
  "PLT", &plt_unit, plt_reg, plt_mod,
  1, 10, 31, 1, 8, 8,
  NULL, NULL, &plt_reset,
  NULL, plt_attach, plt_detach,
  &plt_dib, DEV_DISABLE
};

static t_stat plt_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOFNC;

  return SCPE_OK;
}

static t_stat plt_set_option (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  const int i = (val & OPN_MASK) >> UNIT_V_OPN0;

  if ((i<0) || (i>3)) {
    return SCPE_ARG; /* Surely impossible, but still... */
  }

  plt_xlimit   = plotter_models[i].limit_width / plotter_models[i].step;
  uptr->time   = (INSTR_PER_SEC / plotter_models[i].incr_freq);
  plt_pen_wait = (INSTR_PER_SEC / plotter_models[i].pen_freq);
  
  return SCPE_OK;
}

static int is_pen(enum PLT_DIRN dirn) {
  return ((dirn == PD_UP) || (dirn == PD_DN));
}

static int is_legal(enum PLT_DIRN dirn)
{
  return ((dirn != PD_NULL) && 
          ((!(((dirn & PD_W) && (dirn & PD_E)) ||   /* Can't have both East and West */
              ((dirn & PD_N) && (dirn & PD_S)))) || /* nor both North and South      */
           is_pen(dirn)));                          /* except for the pen up/down    */
}

static int is_limit() {
  return ((plt_xpos < 0) || (plt_xpos >= plt_xlimit));
}

static void plot_data()
{
  int data;
  int bit, limit;

  if (plt_dirn != PD_NULL) {
    if ((plt_unit.flags & UNIT_ASC) ||
        (plt_unit.flags & UNIT_UASC)) {
      
      fprintf(plt_unit.fileref, "%s", pd_names[plt_dirn]);
      if (plt_count > 0) {
        fprintf(plt_unit.fileref, " %d", plt_count);
      }
      fprintf(plt_unit.fileref, "\n");
      plt_unit.pos = ftell(plt_unit.fileref);     
    } else {
      
      if (plt_count > 7) {
        /* Figure out the limit that can be represented
         * by the minimum number of prefix bytes */
        bit = 3;
        do {
          bit += 7;
          limit = 1 << bit;
        } while (limit <= plt_count);
        
        /* Output those prefix bytes */
        do {          
          bit -= 7;
          
          data = ((plt_count >> bit) & 127) | 128;
          
          putc(data, plt_unit.fileref);
        } while (bit > 3);
      }
      data = (((plt_dirn & 15) << 3) |
              (plt_count & 7));
      putc(data, plt_unit.fileref);
      plt_unit.pos = ftell(plt_unit.fileref);     
    }
    plt_dirn  = PD_NULL;
    plt_count = 0;
  }
}

/* IO routine */

static int32 pltio (int32 inst, int32 fnc, int32 dat, int32 dev)
{
  const enum PLT_DIRN direction = (enum PLT_DIRN) fnc;
  
  switch (inst) {                                   /* case on opcode */

  case ioOCP:                                       /* OCP */

    if (is_legal(direction)) {
      
      /* As soon as the pen goes down we must produce data */
      if (direction == PD_DN) {
        plt_phase = SHOWTIME;
      }
      
      if (plt_phase == SHOWTIME) {
        if (direction == plt_dirn) {
          plt_count++;
        } else {
          if ((plt_unit.flags & UNIT_ATT) == 0) {   /* attached? */
            return IORETURN (plt_stopioe, SCPE_UNATT);
          }
          
          plot_data();
          plt_dirn  = direction;
          plt_count = 0;
        }
      }

      /* Keep track of location */
      switch(direction) {
      case PD_N:  plt_ypos++;             break;
      case PD_NE: plt_ypos++; plt_xpos++; break;
      case PD_E:              plt_xpos++; break;
      case PD_SE: plt_ypos--; plt_xpos++; break;
      case PD_S:  plt_ypos--;             break;
      case PD_SW: plt_ypos--; plt_xpos--; break;
      case PD_W:              plt_xpos--; break;
      case PD_NW: plt_ypos++; plt_xpos--; break;
      case PD_UP: plt_pen = FALSE;        break;
      case PD_DN: plt_pen = TRUE;         break;
      default:
        abort(); /* Note - it's trapped above */
      }

      /* Normally wait until we've hit a limit
       * switch and backed off it before starting to 
       * produce data */

      switch (plt_phase) {
      case SHOWTIME:                                       break;
      case LIMIT:   if ( is_limit()) plt_phase = UNLIMIT;  break;
      case UNLIMIT: if (!is_limit()) plt_phase = SHOWTIME; break;
      default:
        abort();
      }

      CLR_INT (INT_PLT);
      plt_busy = TRUE;
      sim_activate (&plt_unit,
                    (is_pen(direction) ? plt_pen_wait : plt_unit.wait));
      
    } else {
      return IOBADFNC (dat);
    }

    break;
    
  case ioSKS:                                       /* SKS */
    switch (fnc) {                                  /* case on fnc */
      
    case 001:                                       /* if not busy */
      if (!plt_busy)
        return IOSKIP (dat);
      break;
      
    case 002:                                       /* if not limit */
      if (!is_limit())
        return IOSKIP (dat);
      break;
      
    case 004:                                       /* if !interrupt */
      if (!TST_INTREQ (INT_PLT))
        return IOSKIP (dat);
      break;
      
    default:
      return IOBADFNC (dat);
    }
    break;
    
  case ioINA:                                       /* INA */
  case ioOTA:                                       /* OTA */
    return IOBADFNC (dat);
    break;
  }                                                 /* end case op */

  return dat;
}

/* Unit service */

t_stat plt_svc (UNIT *uptr)
{
  int32 i;

  SET_INT (INT_PLT);                                  /* interrupt */
  plt_busy = FALSE;

  return SCPE_OK;
}

/* Reset routine */

t_stat plt_reset (DEVICE *dptr)
{
  const int i = (plt_unit.flags & OPN_MASK) >> UNIT_V_OPN0;

  plt_xpos     = PLT_INITIAL_XPOS;
  plt_ypos     = 0;
  plt_busy     = 0;
  plt_dirn     = PD_NULL;
  plt_count    = 0;
  plt_pen      = 0;
  plt_phase    = LIMIT;
  plt_xlimit   = 3400;
  plt_pen_wait = PLT_PEN_WAIT;
  plt_stopioe  = TRUE;

  plt_unit.time = PLT_INCR_WAIT;

  if ((i>=0) && (i<=3)) {
    plt_xlimit    = plotter_models[i].limit_width / plotter_models[i].step;
    plt_unit.time = (INSTR_PER_SEC / plotter_models[i].incr_freq);
    plt_pen_wait  = (INSTR_PER_SEC / plotter_models[i].pen_freq);
  }

  SET_INT (INT_PLT);                                      /* Because not busy */
  CLR_ENB (INT_PLT);                                      /* but not enabled */
  
  sim_cancel (&plt_unit);                                 /* deactivate unit */

  return SCPE_OK;
}

/* Plotter attach routine - set or clear ASC/UASC flags if specified. */

static t_stat plt_attach (UNIT *uptr, CONST char *cptr)
{
  t_stat r;
  
  if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOFNC;
  if ((r = attach_unit (uptr, cptr)))
    return r;
  if (sim_switches & SWMASK ('A'))                        /* -a? ASCII */
    uptr->flags |= UNIT_ASC;
  else if (sim_switches & SWMASK ('U'))                   /* -u? Unix ASCII */
    uptr->flags |= (UNIT_ASC|UNIT_UASC);
  else if (sim_switches & SWMASK ('B'))                   /* -b? binary */
    uptr->flags &= ~(UNIT_ASC|UNIT_UASC);

  return r;
}

/* Detach routine - send any final pending data */

static t_stat plt_detach (UNIT *uptr)
{
  if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOFNC;
  if ((uptr->flags & UNIT_ATT) != 0) {                    /* attached? */
    plot_data();
  }
  
  return detach_unit (uptr);
}
