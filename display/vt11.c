/*
 * $Id: vt11.c,v 1.17 2004/01/24 20:44:46 phil Exp - revised by DAG $
 * Simulator Independent VT11/VS60 Graphic Display Processor Simulation
 * Phil Budne <phil@ultimate.com>
 * September 13, 2003
 * Substantially revised by Douglas A. Gwyn; last edited 05 Aug 2005
 *
 * from EK-VT11-TM-001, September 1974
 *  and EK-VT48-TM-001, November  1976
 * with help from Al Kossow's "VT11 instruction set" posting of 21 Feb 93
 *  and VT48 Engineering Specification Rev B
 *  and VS60 diagnostic test listings, provided by Alan Frisbie
 */

/*
 * Copyright (c) 2003-2004, Philip L. Budne and Douglas A. Gwyn
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the names of the authors shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from the authors.
 */

/*
 *      The VT11 is a calligraphic display-file device used in the GT4x series
 *      of workstations (PDP-11/04,34,40 based).
 *
 *      The VS60 is an improved, extended, upward-compatible version of the
 *      VT11, used in the GT62 workstation (PDP-11/34 based).  It supports
 *      dual consoles (CRTs with light pens), multiple phosphor colors, 3D
 *      depth cueing, and circle/arc generator as options.  We do not know
 *      whether any of these options were ever implemented or delivered.
 *      Apparently a later option substituted a digitizing-tablet correlator
 *      for the light pen.  The VS60 has a 4-level silo (graphic data pipeline)
 *      which for reasons of simplicity is not implemented in this simulation;
 *      the only visible effect is that DZVSC diagnostic tests 110 & 111 will
 *      report failure.
 *
 *      The VSV11/VS11 is a color raster display-file device (with joystick
 *      instead of light pen) with instructions similar to the VT11's but
 *      different enough that a separate emulation should be created by
 *      editing a copy of this source file rather than trying to hack it into
 *      this one.  Very likely, the display (phosphor decay) simulation will
 *      also require revision to handle multiple colors.
 *
 *      There were further models in this series, but they appear to have
 *      little if any compatibility with the VT11.
 *
 *      Much later, DEC produced a display system it called the VS60S, but
 *      that doesn't seem to bear any relationship to the original VS60.
 *
 *      A PDP-11 system has at most one display controller attached.
 *      In principle, a VT11 or VS60 can also be used on a VAX Unibus.
 *
 *      STATUS:
 *
 *      Clipping is not implemented properly for arcs.
 *
 *      This simulation passes all four MAINDEC VS60 diagnostics and the
 *      DEC/X11 VS60 system exerciser, with the following exceptions:
 *
 *      MD-11-DZVSA-A, VS60 instruction test part I, test 161:
 *      Failure to time-out an access to a "nonexistent" bus address, when the
 *      system is configured with so much memory that the probed address
 *      actually responds; this is a deficiency in the diagnostic itself.
 *
 *      MD-11-DZVSB-A, VS60 instruction test part II:
 *      No exceptions.
 *
 *      MD-11-DZVSC-B, VS60 instruction test part III, tests 107,110,111:
 *      Memory address test fails under SIMH, due to SIMH not implementing
 *      KT11 "maintenance mode", in which the final destination address (only)
 *      is relocated.  When SIMH is patched to fix this, the test still fails
 *      due to a bug in the diagnostic itself, namely a call to DPCONV1 which
 *      tests a condition code that is supposed to pertain to R0 but which
 *      hasn't been set up.  Swapping the two instructions before the call to
 *      DPCONV1 corrects this, and then this test passes.
 *      Graphic silo content tests fail, since the silo pipeline is not
 *      simulated; there are no plans to fix this, since it serves no other
 *      purpose in this simulation and would adversely affect performance.
 *
 *      MD-11-DZVSD-B, VS60 visual display test, frame 13:
 *      "O" character sizes are slightly off, due to optimization for raster
 *      display rather than true stroking; there are no plans to change this.
 *
 *      MD-11-DZVSE-A0, XXDP VS60 visual display exerciser:
 *      No visible exceptions.  Light-pen interrupts might not be handled
 *      right, since they're reported as errors and cause display restart.
 *      (XXX  Need to obtain source listing to check this.)
 */

#ifdef DEBUG_VT11
#include <stdio.h>
#endif
#include <string.h>                     /* memset */
#ifndef NO_CONIC_OPT
#include <math.h>                       /* atan2, cos, sin, sqrt */
#endif

#include "display.h"                    /* XY plot interface */
#include "vt11.h"

#define BITMASK(n) (1<<(n))             /* PDP-11 bit numbering */

/* mask for a field */
#define FIELDMASK(START,END) ((1<<((START)-(END)+1))-1)

/* extract a field */
#define GETFIELD(W,START,END) (((W)>>(END)) & FIELDMASK(START,END))

/* extract a 1-bit field */
#define TESTBIT(W,B) (((W) & BITMASK(B)) != 0)

static void *vt11_dptr;
static int vt11_dbit;

#if defined(DEBUG_VT11) || defined(VM_PDP11)

#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif
#define DEVICE void

#define DBG_CALL 1
int vt11_debug;

#if defined(VM_PDP11)
extern void _sim_debug (unsigned int dbits, DEVICE* dptr, const char* fmt, ...);

#define DEBUGF(...) _sim_debug (vt11_dbit, vt11_dptr, ##  __VA_ARGS__)
#else /* DEBUG_VT11 */
#define DEBUGF(...) do {if (vt11_debug & DBG_CALL) { printf(##  __VA_ARGS__); fflush(stdout); };} while (0)
#endif /* defined(DEBUG_VT11) || defined(VM_PDP11) */
#if defined(__cplusplus)
}
#endif
#else

#define DEBUGF(...) 

#endif

/*
 * Note about coordinate signedness and wrapping:
 *
 * The documentation for these devices says confusing things about coordinate
 * wrapping and signedness.  The VT11 maintains 12-bit coordinate registers
 * (wrapping 4096 -> 0), while the VS60 maintains 14-bit coordinate registers.
 * Coordinate arithmetic (such as adding a vector "delta" to the current
 * position) that overflows merely drops the extra bits; this can be treated
 * as use of twos-complement representation for the position registers, whereas
 * the VS60 offset registers and the display file itself use a signed-magnitude
 * representation.  (Except that JMP/JSR-relative delta uses twos-complement!)
 * This simulation tracks position using at least 32 bits including sign; this
 * can overflow only for a pathological display file.
 *
 * Note about scaling and offsets:
 *
 * The VS60 supports character and vector scaling and position offsets.  The
 * X, Y, and Z position register values always include scaling and offsets.
 * It is not clear from the manual whether or not there are two "guard bits",
 * which would better track the virtual position when using a scale factor of
 * 3/4, 1/2, or 1/4.  Most likely, there are no guard bits (this has been
 * confirmed by diagnostic DZVSB test 31).  This simulation maintains position
 * values and offsets both multiplied by PSCALEF, which should be 4 to obtain
 * maximum drawing precision, or 1 to mimic the actual non-guard-bit display
 * hardware.  These internal coordinates are "normalized" (converted to correct
 * virtual CRT coordinates) before being reported via the position/offset
 * registers. The normalized Z position register value's 2 lowest bits are
 * always 0.
 * Example of why this matters:  Set vector scale 1/2; draw successive vectors
 * with delta X = 1, 1, and -2.  With guard bits, the final and original X
 * positions are the same; without guard bits, the final X position is one
 * unit to the left of the original position.  This effect accumulates over a
 * long sequence of vectors, leading to quite visible distortion of the image.
 *
 * Light-pen and edge-interrupt positions always have "on-screen" values.
 */

#ifndef PSCALEF
#if 0   /* XXX  enable only during development, to catch any oversights */
#define PSCALEF 4       /* position scale factor 4 for maximum precision */
#else
#define PSCALEF 1       /* position scale factor 1 for accurate simulation */
#endif
#endif

#define PSCALE(x) ((x) * PSCALEF)
#define PNORM(x)  ((x) / PSCALEF)
/* virtual_CRT_coordinate = PNORM(scaled_value) */

/* VS60 scales points/vectors and characters separately */
#define VSCALE(x) ((PSCALE(vector_scale * (int32)(x)) + ((x)>=0 ? 1 : -1)) / 4)
#define CSCALE(x) ((PSCALE(char_scale * (int32)(x)) + ((x)>=0 ? 1 : -1)) / 4)
/* (The "+ ((x)>=0 ? 1 : -1)" above is needed to pass the diagnostics.) */

#define ABS(x)          ((x) >= 0 ? (x) : -(x))
#define TWOSCOMP(x)     ((x) >= 0 ? (x) : ~(-(x)-1))

enum display_type vt11_display = DISPLAY_TYPE;  /* DIS_VR{14,17,48} */
int vt11_scale = PIX_SCALE;     /* RES_{FULL,HALF,QUARTER,EIGHTH} */
unsigned char vt11_init = 0;    /* set after display_init() called */
#define INIT { if (!vt11_init) { display_init(vt11_display, vt11_scale, vt11_dptr); \
                    vt11_init = 1; vt11_reset(vt11_dptr, vt11_dbit); } }

/* state visible to host */

/* The register and field names are those used in the VS60 manual (minus the
   trailing "flag", "code", "status", or "select"); the VT11 manual uses
   somewhat different names. */

/*
 * Display Program Counter
 * Read/Write (reading returns the *relocated* DPC bits [15:0])
 * DPC address  15:1
 * resume       0
 */
#define DPC stack[8]._dpc               /* Display PC (always even) */
static uint16 bdb = 0;                  /* Buffered Data Bits register;
                                           see comment in vt11_get_dpc() */

/*
 * Mode Parameter Register
 * Read Only, except that writing to it beeps the LK40 keyboard's bell
 * internal stop flag           15
 * graphic mode code            14:11
 * intensity level              10:8
 * LP con. 0 hit flag           7
 * shift out status             6
 * edge indicator               5
 * italics status               4
 * blink status                 3
 * edge flag status             2       (VS60 only)
 * line type register status    1:0
 */
static unsigned char internal_stop = 0; /* 1 bit: stop display */
static unsigned char mode_field = 0;    /* copy of control instr. bits 14-11 */
#define graphic_mode stack[8]._mode     /* 4 bits: sets type for graphic data */
enum gmode { CHAR=0, SVECTOR, LVECTOR, POINT, GRAPHX, GRAPHY, RELPOINT, /* all */
            BSVECT, CIRCLE, ABSVECTOR   /* VS60 only */
};

#define intensity    stack[8]._intens   /* 3 bits: 0 => dim .. 7 => bright */
static unsigned char lp0_hit = 0;       /* 1 bit: light pen #0 detected hit */
static unsigned char so_flag = 0;       /* 1 bit: illegal char. in SO mode */
static unsigned char edge_indic = 0;    /* 1 bit: crossing visible area edge */
#define italics      stack[8]._italics  /* 1 bit: use italic font */
#define blink_ena    stack[8]._blink    /* 1 bit: blink graphic item */
static unsigned char edge_flag = 0;     /* 1 bit: edge intr if enabled (VS60) */
#define line_type    stack[8]._ltype    /* 2 bits: style for drawing vectors */
enum linetype { SOLID=0, LONG_DASH, SHORT_DASH, DOT_DASH };

/*
 * Graphplot Increment and X Position Register
 * Read Only
 * graphplot increment register value   15:10
 * X position register value            9:0
 */
static unsigned char graphplot_step = 0;/* (scaled) graphplot step increment */
static int32         xpos = 0;          /* X position register * PSCALEF */
                                        /* note: offset has been applied! */
static int           lp_xpos;           /* (normalized) */
static int           edge_xpos;         /* (normalized) */

/*
 * Character Code and Y Position Register
 * Read Only
 * character register contents  15:10
 * Y position register value    9:0
 */
static unsigned char char_buf = 0;      /* (only lowest 6 bits reported) */
static int32         ypos = 0;          /* Y position register * PSCALEF */
                                        /* note: offset has been applied! */
static int           lp_ypos;           /* (normalized) */
static int           edge_ypos;         /* (normalized) */

/*
 * Relocate Register (VS60 only)
 * Read/Write
 * spare                                15:12
 * relocate register value[17:6]        11:0
 */
static uint32 reloc = 0;                /* relocation, aligned with DPC */

/*
 * Status Parameter Register (VS60 only)
 * Read Only, except for bit 7 (1 => external stop request)
 * display busy status          15
 * stack overflow status        13
 * stack underflow status       12
 * time out status              11
 * char. rotate status          10
 * char. scale index            9:8
 * external stop flag           7
 * menu status                  6
 * relocated DPC bits [17:16]   5:4
 * vector scale                 3:0
 */
#define busy (!(stopped || lphit_irq || lpsw_irq || edge_irq || char_irq \
                        || stack_over || stack_under || time_out || name_irq))
                                        /* 1 bit: display initiated | resumed */
static unsigned char stack_over = 0;    /* 1 bit: "push" with full stack */
static unsigned char stack_under = 0;   /* 1 bit: "pop" with empty stack */
static unsigned char time_out = 0;      /* 1 bit: timeout has occurred */
#define char_rotate  stack[8]._crotate  /* 1 bit: rotate chars 90 degrees CCW */
#define cs_index     stack[8]._csi      /* character scale index 0..3 */
static unsigned char ext_stop = 0;      /* 1 bit: stop display */
#define menu         stack[8]._menu     /* 1 bit: VS60 graphics in menu area */
#define vector_scale stack[8]._vscale   /* non-character scale factor * 4 */

/*
 * X Offset Register (VS60 only)
 * Read/Write
 * upper X position bits        15:12   (read)
 * sign of X dynamic offset     13      (write)
 * X dynamic offset             11:0
 */
static unsigned char    s_xoff = 0;     /* sign bit for xoff (needed for -0) */
static int32            xoff = 0;       /* X offset register * PSCALEF */

/*
 * Y Offset Register (VS60 only)
 * Read/Write
 * upper Y position bits        15:12   (read)
 * sign of Y dynamic offset     13      (write)
 * Y dynamic offset             11:0
 */
static unsigned char    s_yoff = 0;     /* sign bit for yoff (needed for -0) */
static int32            yoff = 0;       /* Y offset register * PSCALEF */

/*
 * Associative Name Register (VS60 only)
 * Write Only
 * search code change enable    14
 * search code                  13:12
 * name change enable           11
 * associative name             10:0
 */
static unsigned char search = 0;        /* 00=> no search, no interrupt
                                           01 => intr. on 11-bit compare
                                           10 => intr. on high-8-bit compare
                                           11 => intr. on high-4-bit compare */
static unsigned      assoc_name = 0;    /* compare value */

/*
 * Slave Console/Color Register (VS60 only)
 * Read/Write *
 * inten enable con. 0                  15
 * light pen hit flag con. 0            14      *
 * LP switch on flag con. 0             13      *
 * LP switch off flag con. 0            12      *
 * LP flag intr. enable con. 0          11
 * LP switch flag intr. enable con. 0   10
 * inten enable con. 1                  9
 * light pen hit flag con. 1            8       *
 * LP switch on flag con. 1             7       *
 * LP switch off flag con. 1            6       *
 * LP flag intr. enable con. 1          5
 * LP switch flag intr. enable con. 1   4
 * color                                3:2
 *
 *      * indicates that maintenance switch 3 must be set to write these bits;
 *        the other bits are not writable at all
 */
#define int0_scope      stack[8]._inten0 /* enable con 0 for all graphic data */
/* lp0_hit has already been defined, under Mode Parameter Register */
static unsigned char    lp0_down = 0;   /* 1 bit: LP #0 switch was depressed */
static unsigned char    lp0_up = 0;     /* 1 bit: LP #0 switch was released */
#define lp0_intr_ena    stack[8]._lp0intr /* generate interrupt on LP #0 hit */
#define lp0_sw_intr_ena stack[8]._lp0swintr /* generate intr. on LP #0 sw chg */
#define int1_scope      stack[8]._inten1 /* enable con 1 for all graphic data */
/* following 2 flags only mutable via writing this register w/ MS3 set: */
static unsigned char    lp1_hit = 0;    /* 1 bit: light pen #1 detected hit */
static unsigned char    lp1_down = 0;   /* 1 bit: LP #1 switch was depressed */
static unsigned char    lp1_up = 0;     /* 1 bit: LP #1 switch was released */
#define lp1_intr_ena    stack[8]._lp1intr /* generate interrupt on LP #1 hit */
#define lp1_sw_intr_ena stack[8]._lp1swintr /* generate intr. on LP #1 sw chg */

enum scolor { GREEN=0, YELLOW, ORANGE, RED };
#define color           stack[8]._color /* 2 bits: VS60 color option */

/*
 * Name Register (VS60 only)
 * Read Only
 * name/assoc name match flag   15
 * search code                  13:12
 * name                         10:0
 */
static unsigned char name_irq = 0;      /* 1 bit: name matches associative nm */
                                        /* (always interrupts on name match!) */
/* search previously defined, under Associative Name Register */
#define name stack[8]._name             /* current name from display file */

/*
 * Stack Data Register (VS60 only)
 * Read Only
 * stack data   15:0                    (as selected by Stk. Addr./Maint. Reg.)
 *
 * On the actual hardware there are 2 32-bit words per each of 8 stack levels.
 * At the PDP-11 these appear to be 4 16-bit words ("stack bytes") per level.
 */
/* It is important to note that setting the stack level via SAR doesn't change
   the parameters currently in effect; only JSR/POPR does that.  To speed up
   JSR/POPR, the current state is implemented as an extra stack frame, so that
   push/pop is done by copying a block rather than lots of individual variables.
   There are thus 9 stack elements, 8 stack entries [0..7] and the current state
   [8].  Mimicking the actual hardware, the stack level *decreases* upon JSR.
 */

static struct frame
        {
        vt11word      _dpc;             /* Display Program Counter (even) */
        unsigned      _name;            /* (11-bit) name from display file */
        enum gmode    _mode;            /* 4 bits: sets type for graphic data */
        unsigned char _vscale;          /* non-character scale factor * 4 */
        unsigned char _csi;             /* character scale index 0..3 */
        unsigned char _cscale;          /* character scale factor * 4 */
        unsigned char _crotate;         /* rotate chars 90 degrees CCW */
        unsigned char _intens;          /* intensity: 0 => dim .. 7 => bright */
        enum linetype _ltype;           /* line type (long dash, etc.) */
        unsigned char _blink;           /* blink enable */
        unsigned char _italics;         /* italicize characters */
        unsigned char _so;              /* currently in shift-out mode */
        unsigned char _menu;            /* VS60 graphics in menu area */
        unsigned char _cesc;            /* perform POPR on char. term. match */
        unsigned char _edgeintr;        /* generate intr. on edge transition */
        unsigned char _lp1swintr;       /* generate intr. on LP #1 switch chg */
        unsigned char _lp0swintr;       /* generate intr. on LP #0 switch chg */
        unsigned char _lp1intr;         /* generate interrupt on LP #1 hit */
        unsigned char _inten1;          /* blank cons. 1 for all graphic data */
        unsigned char _lp0intr;         /* generate interrupt on LP #0 hit */
        unsigned char _inten0;          /* blank cons. 0 for all graphic data */
        unsigned char _bright;          /* visually indicate hit on entity */
        unsigned char _stopintr;        /* generate interrupt on intern. stop */
        enum scolor   _color;           /* scope display color (option) */
        unsigned char _zdata;           /* flag: display file has Z coords */
        unsigned char _depth;           /* flag: display Z using depth cue */
        } stack[9] = { { 0, 0, CHAR, 0, 0, 0, 0, 0, SOLID, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GREEN, 0, 0 },
                       { 0, 0, CHAR, 0, 0, 0, 0, 0, SOLID, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GREEN, 0, 0 },
                       { 0, 0, CHAR, 0, 0, 0, 0, 0, SOLID, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GREEN, 0, 0 },
                       { 0, 0, CHAR, 0, 0, 0, 0, 0, SOLID, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GREEN, 0, 0 },
                       { 0, 0, CHAR, 0, 0, 0, 0, 0, SOLID, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GREEN, 0, 0 },
                       { 0, 0, CHAR, 0, 0, 0, 0, 0, SOLID, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GREEN, 0, 0 },
                       { 0, 0, CHAR, 0, 0, 0, 0, 0, SOLID, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GREEN, 0, 0 },
                       { 0, 0, CHAR, 0, 0, 0, 0, 0, SOLID, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GREEN, 0, 0 },
                       { 0, 0, CHAR, 4, 1, 4, 0, 4, SOLID, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 1, 0, GREEN, 0, 0 },
                     };

#define char_scale      stack[8]._cscale /* character scale factor * 4 */
                                        /* _cscale must track _csi! */
static const unsigned char csi2csf[4] = { 2, 4, 6, 8 }; /* maps cs_index to " */
#define shift_out       stack[8]._so    /* flag: using shift-out char. set */
#define char_escape     stack[8]._cesc  /* perform POPR on char. term. match */
#define edge_intr_ena   stack[8]._edgeintr /* generate intr. on edge transit */
#define lp_intensify    stack[8]._bright /* if VT11, 20us bright spot;
                                            if VS60, brighten the entity */
#define stop_intr_ena   stack[8]._stopintr /* generate intr. on internal stop */
#define file_z_data     stack[8]._zdata /* flag: display file has Z coords */
#define depth_cue_proc  stack[8]._depth /* flag: display Z using depth cue */

/*
 * Character String Terminate Register (VS60 only)
 * Read/Write
 * char. term. reg. enable      7
 * character terminate code     6:0
 */
static int char_term = 0;               /* char. processing POPRs after this */

/*
 * Stack Address/Maintenance Register (VS60 only)
 * Read/Write
 * maint. sw. 4                 15
 * maint. sw. 3                 14
 * maint. sw. 2                 13
 * maint. sw. 1                 12
 * offset mode status           10
 * jump to subr. ?rel. status   9       (diagnostic requires this be JSR abs.!)
 * word 2 status                8
 * word 1 status                7
 * word 0 status                6
 * stack reset status           5
 * stack level select           4:2     (manual has this messed up)
 * stack halfword select        1:0     (manual has this messed up)
 */
static unsigned char maint4 = 0;        /* 1 bit: maintenance switch #4 */
static unsigned char maint3 = 0;        /* 1 bit: maintenance switch #3 */
static unsigned char maint2 = 0;        /* 1 bit: maintenance switch #2 */
static unsigned char maint1 = 0;        /* 1 bit: maintenance switch #1 */
static unsigned char offset = 0;        /* 1 bit: last data loaded offsets */
static unsigned char jsr = 0;           /* 1 bit: last control was JSR ?rel. */
static int word_number = -2;            /* tracks multiple data words */
#define CONTROL_MODE()  (word_number == -1)     /* true when in control mode */
#define DATA_MODE()     (word_number >= 0)      /* true when in data mode */
static struct frame *sp = &stack[8];    /* -> selected stack frame, or TOS */
#define STACK_EMPTY (sp == &stack[8])   /* "TOS" */
#define STACK_FULL  (sp == &stack[0])   /* "BOS" */
static unsigned char stack_sel = 8<<2;  /* 8 levels, 4 PDP-11 words per level */
                                        /* stack_sel must track sp and TOS! */

/*
 * Z Position Register, Depth Cue Option (VS60 only)
 * Read/Write
 * Z position register value[13:2]      11:0
 */
static int32 zpos = 0;                  /* (Z "position" reg. * 4) * PSCALEF */
                                        /* note: offset has been applied! */
static int32 lp_zpos;                   /* (scaled) */
static int32 edge_zpos;                 /* (scaled) */

/*
 * Z Offset Register, Depth Cue Option (VS60 only)
 * Read/Write
 * sign of X dynamic offset     15      (read)  (VT48 manual has this confused)
 * sign of Y dynamic offset     14      (read)  (VT48 manual has this confused)
 * sign of Z dynamic offset     13
 * Z dynamic offset             11:0
 */
static unsigned char    s_zoff = 0;     /* sign bit for zoff (needed for -0) */
static int32            zoff = 0;       /* Z offset register * PSCALEF */

/*
 * Invisible state:
 */
static unsigned char char_irq = 0;      /* intr. on illegal char in SO mode */
static unsigned char lphit_irq = 0;     /* intr. on light-pen hit */
static unsigned char lpsw_irq = 0;      /* intr. on tip-switch state change */
static unsigned char edge_irq = 0;      /* intr. on edge transition */

static unsigned char lp0_sw_state = 0;  /* last known LP tip-switch state */
static unsigned char blink_off = 0;     /* set when blinking graphics is dark */
static unsigned char finish_jmpa = 0;   /* reminder to fetch JMPA address */
static unsigned char finish_jsra = 0;   /* reminder to fetch JSRA address */

static unsigned char more_vect = 0;     /* remembers LP hit in middle of vec. */
static unsigned char more_arc = 0;      /* remembers LP hit in middle of arc */
static int32 save_x0, save_y0, save_z0, save_x1, save_y1, save_z1;
                                        /* CRT coords for rest of vector */

static unsigned char lp_suppress = 0;   /* edge columns of char. (VT11 only) */
static unsigned char stroking = 0;      /* set when drawing VS60 char strokes */
static unsigned char skip_start = 0;    /* set between vis. char./arc strokes */

static unsigned char stopped = 1;       /* display processor frozen */
static unsigned char sync_period = 0;   /* frame sync period (msec) */
static unsigned char refresh_rate = 0;  /* 2 bits:
                                           00 => continuous display refresh
                                           01 => 30 fps (60 fps if VT11)
                                           10 => 40 fps (VS60)
                                           11 => external sync (VS60) */

#if 0   /* this is accurate in simulated "real" time */
#define BLINK_COUNT 266                 /* 266 milliseconds */
#else   /* this looks better in actual real time (adjust for your host speed) */
#define BLINK_COUNT 67                  /* 67 milliseconds */
#endif

unsigned char vt11_csp_w = VT11_CSP_W;  /* horizontal character spacing */
unsigned char vt11_csp_h = VT11_CSP_H;  /* vertical character spacing */
/* VS60 spacing depends on char scale; above are right for char scale x1 */

/* VS60 has a menu area to the right of the "main working surface" */
#define MENU_OFFSET (1024 + VR48_GUTTER)        /* left edge of menu on CRT */
#define VR48_WIDTH (MENU_OFFSET + 128)  /* X beyond this is not illuminated */

static int reduce;                      /* CRT units per actual pixel */
static int x_edge;                      /* 1023 or VR48_WIDTH-1, depending */
static int y_edge;                      /* 767 or 1023, depending on display */
#define ONCRT(x,y)      ((x) >= 0 && (x) <= x_edge && (y) >= 0 && (y) <= y_edge)

/*
 * Clipping-specific stuff.
 * When a vector crosses the edge of the viewing window, the "edge flag" is set
 * and the "edge indicator" indicates whether the first point on the visible
 * segment is clipped.  Apparently the VT11 does not draw the visible segment,
 * but the VS60 will draw the segment (after a resume from an edge interrupt,
 * if the interrupt was enabled).  The VS60 will also post a second interrupt
 * corresponding to the end of the visible segment, after setting the edge flag
 * (again) and setting the edge indicator according to whether the last point
 * on the visible segment was clipped.
 * Note: a light-pen hit is possible on a drawn clipped segment.
 */
static int clip_vect = 0;       /* set when clipped coords saved; bit-coded:
                                        1 => entry clipped
                                        2 => exit clipped */
static int clip_i;                      /* saved "intensify" bit */
static int32 clip_x0, clip_y0, clip_z0; /* CRT coords for entry point */
static int32 clip_x1, clip_y1, clip_z1; /* CRT coords for exit point */

/*
 * Uncertain whether VS60 edge transitions in menu area are flagged and whether
 * clipping takes menu width into account.  Three possibilities:
 */
#define CLIPYMAX        y_edge
#if 0                           /* menu area never clipped (seems wrong) */
#define CLIPXMAX        1023
#define ONSCREEN(x,y)   (menu || ((x) >= 0 && (x) <= CLIPXMAX \
                               && (y) >= 0 && (y) <= CLIPYMAX))
#elif 0                         /* menu area correctly clipped (unlikely) */
#define CLIPXMAX        (menu ? 127 : 1023)
#define ONSCREEN(x,y)   ((x) >= 0 && (x) <= CLIPXMAX \
                      && (y) >= 0 && (y) <= CLIPYMAX)
#else                           /* menu area clipped same as main area */
#define CLIPXMAX        1023
#define ONSCREEN(x,y)   ((x) >= 0 && (x) <= CLIPXMAX \
                      && (y) >= 0 && (y) <= CLIPYMAX)
#endif

static void lineTwoStep(int32, int32, int32, int32, int32, int32);
                                        /* forward reference */

/*
 * calls to read/write VT11/VS60 CSRs
 *
 * Presumably the host looks at our state less often than we do(!)
 * so we keep it in a form convenient to us, rather than as bit fields
 * packed into "registers".  The simulated VT48 register contents are
 * converted to/from our internal variables by the following functions.
 */

int32
vt11_get_dpc(void)
{   INIT
    /*
     * The VT48 manual says that Maintenance Switch 1 causes the Buffered
     * Data Bits register to be "entered into the DPC" so it can be
     * examined by reading the DPC address, but details of when and how
     * often that happens are not provided.  Examination of the diagnostic
     * test listings shows that relocation is applied and that only the DPC
     * is involved when this switch is set.
     */
    return ((maint1 ? bdb : DPC) + reloc) & 0177777;
}

void
vt11_set_dpc(uint16 d)
{   INIT
    bdb = d;                            /* save all bits in case maint1 used */
    DEBUGF("set DPC 0%06o\r\n", (unsigned)d);
    /* Stack level is unaffected, except that stack_sel==037 goes to 040; this
       fudge is necessary to pass DZVSC test 3, which misleadingly calls it
       setting top-of-stack upon START (vt11_set_dpc(even)).  If one instead
       were to set TOS upon START, then several DZVSC diagnostics would fail! */
    if (VS60 && !STACK_EMPTY && GETFIELD(stack_sel,1,0) == 3) {
        stack_sel = GETFIELD(stack_sel,4,2) + 1;        /* 1..8 */
        sp = &stack[stack_sel];         /* [1..8] */
        stack_sel <<= 2;
    }
    if (!TESTBIT(d,0)) {                /* START */
        DPC = d;                        /* load DPC */
        sync_period = 0;
        ext_stop = 0;
        /* the following seem reasonable, but might be wrong */
        finish_jmpa = finish_jsra = jsr = 0;
        word_number = -2;
        clip_vect = 0;                  /* discard clipped vector data */
#if 0   /* probably accurate mimicry, but ugly behavior */
        if (edge_irq) {
            xpos = PSCALE(edge_x);
            ypos = PSCALE(edge_y);
        }
#endif
    } else {                            /* RESUME (after intr); DPC unchanged */
        /* if resuming from LP hit interrupt, finish drawing rest of vector */
        if (more_vect) {
            unsigned char save_ena = lp0_intr_ena;
            lp0_intr_ena = 0;           /* one hit per vector is plenty */
            lphit_irq = 0;              /* or else lineTwoStep aborts again! */
            /* line_counter is intact; draw rest of visible vector */
            lineTwoStep(save_x0, save_y0, save_z0, save_x1, save_y1, save_z1);
            lp0_intr_ena = save_ena;
        }
        if (more_arc) {                 /* remainder of chord was just drawn */
            unsigned char save_ena = lp0_intr_ena;
            lp0_intr_ena = 0;           /* one hit per arc is plenty */
            lphit_irq = 0;              /* or else lineTwoStep aborts again! */
            /* line_counter is intact; draw rest of visible arc */
            /*XXX  not yet implemented  [conic{23}(<saved params>) needed]*/
            lp0_intr_ena = save_ena;
        }
        if (!maint2)                    /* kludge to satify diagnostic test */
            ext_stop = 0;
    }
    stopped = internal_stop = time_out = stack_over = stack_under = 0;
    more_vect = more_arc = stroking = skip_start = 0;
    so_flag = edge_indic = edge_flag = lp0_hit = lp1_hit = lp_suppress = 0;
    lp0_down = lp0_up = lp1_down = lp1_up = 0;
    char_irq = lphit_irq = lpsw_irq = edge_irq = name_irq = 0;
    /* next vt11_cycle() will perform a fetch */
}

int32
vt11_get_mpr(void)
{
    int32 ret;
    INIT
    ret = (internal_stop<<15) | (mode_field<<11) | (intensity<<8) |
          (lp0_hit<<7) | (so_flag<<6) | (edge_indic<<5) | (italics<<4) |
          (blink_ena<<3) | line_type;

    if (VS60)
        ret |= edge_flag<<2;

    return ret;
}

void
vt11_set_mpr(uint16 d)
{   INIT
    /* beeps the "bell" on the LK40 keyboard */
#if 0   /* probably doesn't hurt to do it for the VS60 also */
    if (VT11)   /* according to the VS60 specs */
#endif
        display_beep();
}

int32
vt11_get_xpr(void)
{
    int32 pos;
    INIT
    pos = lphit_irq ? lp_xpos : edge_irq ? edge_xpos : PNORM(xpos);
    return (graphplot_step << 10) | GETFIELD(TWOSCOMP(pos),9,0);
}

void
vt11_set_xpr(uint16 d)
{   INIT
    DEBUGF("set XPR: no effect\r\n");
}

int32
vt11_get_ypr(void)
{
    int32 pos;
    INIT
    pos = lphit_irq ? lp_ypos : edge_irq ? edge_ypos : PNORM(ypos);
    return (GETFIELD(char_buf,5,0) << 10) | GETFIELD(TWOSCOMP(pos),9,0);
}

void
vt11_set_ypr(uint16 d)
{   INIT
    DEBUGF("set YPR: no effect\r\n");
}

/* All the remaining registers pertain to the VS60 only. */

int32
vt11_get_rr(void)
{   INIT
    return reloc >> 6;
}

void
vt11_set_rr(uint16 d)
{   INIT
    reloc = (uint32)GETFIELD(d,11,0) << 6;
}

int32
vt11_get_spr(void)
{   INIT
    return (busy<<15) | (stack_over<<13) | (stack_under<<12) | (time_out<<11) |
           (char_rotate<<10) | (cs_index<<8) | (ext_stop<<7) |
           (menu<<6) | (((DPC + reloc) & 0600000L) >> 12) | vector_scale;
}

void
vt11_set_spr(uint16 d)
{   INIT
    ext_stop = TESTBIT(d,7);    /* stop occurs at end of next display cycle */

    if (ext_stop /* not maskable */) {
        stopped = 1;                    /* (asynchronous with display cycle) */
        vt_stop_intr();                 /* post stop interrupt to host */
    }
}

int32
vt11_get_xor(void)
{
    int32 off, pos;
    INIT
    off = PNORM(xoff);
    pos = lphit_irq ? lp_xpos : edge_irq ? edge_xpos : PNORM(xpos);
    return (GETFIELD(TWOSCOMP(pos),13,10)<<12) | GETFIELD(ABS(off),11,0);
}

void
vt11_set_xor(uint16 d)
{   INIT
    xoff = PSCALE(GETFIELD(d,11,0));
    s_xoff = TESTBIT(d,13);
    if (s_xoff)
        xoff = -xoff;
}

int32
vt11_get_yor(void)
{
    int32 off, pos;
    INIT
    off = PNORM(yoff);
    pos = lphit_irq ? lp_ypos : edge_irq ? edge_ypos : PNORM(ypos);
    return (GETFIELD(TWOSCOMP(pos),13,10)<<12) | GETFIELD(ABS(off),11,0);
}

void
vt11_set_yor(uint16 d)
{   INIT
    yoff = PSCALE(GETFIELD(d,11,0));
    s_yoff = TESTBIT(d,13);
    if (s_yoff)
        yoff = -yoff;
}

int32
vt11_get_anr(void)
{   INIT
    DEBUGF("get ANR: no effect\r\n");
    return (search << 12) | assoc_name; /* [garbage] */
}

void
vt11_set_anr(uint16 d)
{   INIT
    if (TESTBIT(d,14))
        search = GETFIELD(d,13,12);
    if (TESTBIT(d,11))
        assoc_name = GETFIELD(d,10,0);
}

int32
vt11_get_scr(void)
{   INIT
    return (int0_scope<<15) | (lp0_hit<<14) | (lp0_down<<13) | (lp0_up<<12) |
           (lp0_intr_ena<<11) | (lp0_sw_intr_ena<<10) | (int1_scope<<9) |
           (lp1_hit<<8) | (lp1_down<<7) | (lp1_up<<6) | (lp1_intr_ena<<5) |
           (lp1_sw_intr_ena<<4) | (color << 2);
}

void
vt11_set_scr(uint16 d)
{   INIT
    if (maint3) {
        if (TESTBIT(d,14) && lp0_intr_ena) {
            if (!lphit_irq) {   /* ensure correct position registers reported */
                lp_xpos = PNORM(xpos);
                lp_ypos = PNORM(ypos);
                lp_zpos = PNORM(zpos);
            }
            lp0_hit = lphit_irq = 1;
        }
        if (TESTBIT(d,13)) {
            lp0_down = 1;       /* the manual seems to have this backward */
            if (lp0_sw_intr_ena)
                lpsw_irq = 1;
        }
        if (TESTBIT(d,12)) {
            lp0_up = 1;         /* the manual seems to have this backward */
            if (lp0_sw_intr_ena)
                lpsw_irq = 1;
        }
        if (TESTBIT(d,8) && lp1_intr_ena) {
            if (!lphit_irq) {   /* ensure correct position registers reported */
                lp_xpos = PNORM(xpos);
                lp_ypos = PNORM(ypos);
                lp_zpos = PNORM(zpos);
            }
            lp1_hit = lphit_irq = 1;
        }
        if (TESTBIT(d,7)) {
            lp1_down = 1;
            if (lp1_sw_intr_ena)
                lpsw_irq = 1;
        }
        if (TESTBIT(d,6)) {
            lp1_up = 1;
            if (lp1_sw_intr_ena)
                lpsw_irq = 1;
        }
        if (lpsw_irq || lphit_irq /* && DATA_MODE() */)
            vt_lpen_intr();
    }
}

int32
vt11_get_nr(void)
{   INIT
    return (name_irq<<15) | (search<<12) | name;
}

void
vt11_set_nr(uint16 d)
{   INIT
    DEBUGF("set NR: no effect\r\n");
}

int32
vt11_get_sdr(void)
{
    struct frame *p;
    INIT
    p = &stack[GETFIELD(stack_sel,4,2)];        /* [0..7], 8 (TOS) => 0 */
    switch (GETFIELD(stack_sel,1,0)) {  /* 16-bit "byte" within frame */
    case 0:
        return p->_dpc;                 /* DPC bit#0 is always 0 */

    case 1:
        return (p->_name << 4) | p->_mode;

    case 2:
        return (p->_italics << 15) | (p->_vscale << 11) | (p->_csi << 9) |
               (p->_crotate << 7) | (p->_intens << 4) | ((int)p->_color << 2) |
               p->_ltype;

    case 3:
        return (p->_blink << 15) | (p->_so << 14) | (p->_menu << 13) |
               (p->_cesc << 12) | (p->_edgeintr << 11) | (p->_zdata << 10) |
               (p->_depth << 8) | (p->_lp1swintr << 7) |
               (p->_lp0swintr << 6) | (p->_lp1intr << 5) | (p->_inten1 << 4) |
               (p->_lp0intr << 3) | (p->_inten0 << 2) | ((!p->_bright) << 1) |
               p->_stopintr;
    }
    /*NOTREACHED*/
    return 0;
}

void
vt11_set_sdr(uint16 d)
{   INIT
    DEBUGF("set SDR: no effect\r\n");
}

int32
vt11_get_str(void)
{   INIT
    return char_term;
}

void
vt11_set_str(uint16 d)
{   INIT
    if (TESTBIT(d,7))
        char_term = GETFIELD(d,6,0);
}

int32
vt11_get_sar(void)
{
    int32 ret;
    INIT
    ret = (maint4<<15) | (maint3<<14) | (maint2<<13) | (maint1<<12) |
          (offset<<10) | (jsr<<9) | stack_sel /*includes bit 5=TOS [level 8]*/;
    switch (word_number) {
    case -1:                            /* control mode reported as word 0,
                                           according to VT48 ES */
    case 0:
        ret |= 1<<6;
        break;
    case 1:
        ret |= 1<<7;
        break;
    case 2:
        ret |= 1<<8;
        break;
    /* others (including -1) not reportable */
    }
    return ret;
}

void
vt11_set_sar(uint16 d)
{   INIT
    maint4 = TESTBIT(d,15);             /* 1 => synch. processing pipeline */
    maint3 = TESTBIT(d,14);             /* 1 => copy delta,tangent to x,y pos */
    maint2 = TESTBIT(d,13);             /* 1 => set single-step mode */
    maint1 = TESTBIT(d,12);             /* 1 => vt11_get_dpc will return bdb */
    if (TESTBIT(d,5)) {
        sp = &stack[8];                 /* reset stack pointer to TOS */
        stack_sel = (8<<2)              /* TOS amounts to level 8 */
                  | TESTBIT(stack_sel,0);       /* preserve PDP-11 word sel. */
    } else {
        stack_sel = GETFIELD(d,4,0);
        sp = &stack[GETFIELD(stack_sel,4,2)];   /* [0..7] */
    }
}

/* registers used with the VS60 depth cueing option */

/*
 * Since there is no support for hardware 3D rotation or viewing transform, the
 * only effect of the Z coordinate is to modulate beam intensity along a vector
 * to give the illusion that greater Z coordinates are closer (brighter).
 * This is known as "depth cueing" and is implemented in dintens().
 */

int32
vt11_get_zpr(void)
{
    int32 pos;
    INIT
    pos = lphit_irq ? lp_zpos : edge_irq ? edge_zpos : PNORM(zpos);
    return GETFIELD(TWOSCOMP(pos),13,2);
}

void
vt11_set_zpr(uint16 d)
{   INIT
    DEBUGF("set ZPR: no effect\r\n");
}

int32
vt11_get_zor(void)
{
    int32 off, ret;
    INIT
    off = PNORM(zoff);
    ret = GETFIELD(ABS(off),11,0);
    if (s_xoff)                         /* (VT48 manual has this confused) */
        ret |= 1<<15;
    if (s_yoff)                         /* (VT48 manual has this confused) */
        ret |= 1<<14;
    if (s_zoff)
        ret |= 1<<13;
    return ret;
}

void
vt11_set_zor(uint16 d)
{   INIT
    zoff = PSCALE(GETFIELD(d,11,0));
    s_zoff = TESTBIT(d,13);
    if (s_zoff)
        zoff = -zoff;
}

void
vt11_reset(void *dev, int debug)
{
    if (dev) {
        vt11_dptr = dev;
        vt11_dbit = debug;
        }

    /* make sure display code has been initialized */
    if (!vt11_init)     /* (SIMH invokes before display type is set) */
        return;                         /* wait until last moment */

    if (VS60) {
        /* VS60 character spacing depends on char scale; these are for x1 */
        vt11_csp_w = 14;                /* override VT11 options */
        vt11_csp_h = 24;
    } /* else assume already set up for desired VT11 behavior */

    x_edge = display_xpoints() - 1;
    y_edge = display_ypoints() - 1;
    reduce = display_scale();

    /* reset VT11/VT48 to initial default internal state: */

    /* clear interrupts, BDB, etc. */
    vt11_set_dpc(0);
    /* some of the following should probably be moved to vt11_set_dpc([even]) */
    stopped = int0_scope = 1;           /* idle, console 0 enabled */
    lp0_sw_state = display_lp_sw;       /* sync with mouse button #1 */
    shift_out = int1_scope = stop_intr_ena = blink_off = 0;
    italics = blink_ena = char_rotate = menu = search = offset = 0;
    lp0_sw_intr_ena = lp1_sw_intr_ena = lp0_intr_ena = lp1_intr_ena = 0;
    file_z_data = edge_intr_ena = depth_cue_proc = char_escape = 0;
    maint1 = maint2 = maint3 = maint4 = 0;
    refresh_rate = 0;
    char_buf = char_term = 0;
    assoc_name = name = 0;
    reloc = 0;
    xpos = ypos = zpos = xoff = yoff = zoff = 0;
    s_xoff = s_yoff = s_zoff = 0;
    graphplot_step = 0;
    mode_field = 0;
    graphic_mode = CHAR;
    line_type = SOLID;
    color = GREEN;
    lp_intensify = 1;
    cs_index = 1;
    char_scale = vector_scale = 4;
    intensity = 4;
    sp = &stack[8];
    stack_sel = 8<<2;                   /* PDP-11 word selector also cleared */

    /* following necessary in case the stack is inspected via stack data reg. */
    {   int i;
        for (i = 0; i < 8; ++i)
            memset(&stack[i], 0, sizeof(struct frame));
    }
}

/* VS60 display subroutine support (see stack layout for SDR, above) */

static void
push()
{
    stack_over = STACK_FULL;            /* BOS? */
    if (!stack_over) {
        --sp;
        *sp = stack[8];                 /* copy current parameters */
                                        /* (including *old* DPC) */
        stack_sel -= 1<<2;
        /* XXX  should stack_sel stack-byte bits be cleared? */
    }
    /* else will generate interrupt soon after return */
}

static void
pop(int restore)
{
    stack_under = STACK_EMPTY;          /* TOS? */
    if (!stack_under) {
        stack[8] = *sp;                 /* restore parameters (including DPC) */
        ++sp;
        stack_sel += 1<<2;
        /* XXX  should stack_sel stack-byte bits be cleared? maybe for TOS? */
    }
    /* else will generate interrupt soon after return */
}

/* compute depth-cued display intensity from current display-file intensity */

int
dintens(int32 z)
{
    int i = intensity;

    if (depth_cue_proc) {               /* apply depth cue */
        i += i * z / 1024;              /* XXX  is z scaled properly? */
        if (i > 7)
                i = 7;
        else if (i < 0)
                i = 0;
    }
    i += DISPLAY_INT_MAX - 7;
    return i >= DISPLAY_INT_MIN ? i : DISPLAY_INT_MIN;
}

/*
 * Note:  It would be more efficient to work directly with display intensity
 * levels than with Z coordinates, since the vast majority of dintens()
 * computations result in the same display intensity level as the previous
 * such computation.  However, compared to the rest of the processing per
 * pixel, this computation doesn't seem too expensive, so optimization isn't
 * necessary.
 */

/* illuminate pixel in raster image */

static void
illum3(int32 x, int32 y, int32 z)
                                /* virtual CRT units (offset and normalized) */
{
    int i;                              /* display intensity level */

    /* don't update position registers! */

    /* coords might be outside viewable area, e.g. clipped italic character */
    if (!ONCRT(x, y) || !int0_scope)
        return;

    if (blink_ena && blink_off)         /* blinking & in dark phase */
        return;

    i = dintens(z);

    if (display_point((int)x, (int)y, i, 0)    /* XXX VS60 might switch color */
        /* VT11, per maintenance spec, has threshold 6 for CHAR, 4 for others */
        /* but the classic Lunar Lander uses 3 for its menu and thrust bar! */
        /* I seem to recall that both thresholds were 4 for the VS60 (VR48). */
#if 0
        && (i >= (DISPLAY_INT_MAX-1)    /* (using i applies depth cueing) */
            || (graphic_mode != CHAR && i >= (DISPLAY_INT_MAX-3)))
#else
        /* The following imposes thresholds of 3 for all graphic objects. */
        && (i >= (DISPLAY_INT_MAX-4))   /* (using i applies depth cueing) */
#endif
        && !lp_suppress) {
        lp0_hit = 1;
        if (lp0_intr_ena)
            lphit_irq = 1;              /* will lead to an interrupt */
        /*
         * Save LP hit coordinates so CPU can look at them; the virtual position
         * registers cannot be reported on LP interrupt, since they track the
         * (pre-clipping) end of the vector that was being drawn.
         */
        lp_xpos = x;
        if (menu)
            lp_xpos -= MENU_OFFSET;
        lp_ypos = y;
        lp_zpos = z;
        if (lp_intensify)               /* [technically shouldn't exceed max] */
            display_point((int)x, (int)y, DISPLAY_INT_MAX, 0);
                /* XXX  appropriate for VT11; what about VS60?  chars? */
    }
}

#define illum2(x,y)     illum3(x, y, PNORM(zpos))       /* may be depth cued */
                        /* the extra overhead if not depth cueing is not much */

static void
point3(int i, int32 x1, int32 y1, int32 z1, int detect_edge)
                                /* VSCALEd, offset coordinates (z1 * 4) */
{
    int32 x0 = PNORM(xpos), y0 = PNORM(ypos);

    if (detect_edge) {
        edge_indic =  ONSCREEN(x0, y0);         /* first test */
        edge_flag  = !ONSCREEN(x0, y0);         /* first test */
    } else {
        edge_indic = 0;
        edge_flag  = 0;
    }
    xpos = x1;
    ypos = y1;
    zpos = z1;
    x1 = PNORM(xpos);
    y1 = PNORM(ypos);
    z1 = PNORM(zpos);
    if (detect_edge) {
        edge_indic &= !ONSCREEN(x1, y1);        /* second test */
        edge_flag  &=  ONSCREEN(x1, y1);        /* second test */
        edge_flag |= edge_indic;
        if (edge_flag) {
            if (edge_intr_ena) {
                edge_xpos = x1;
                edge_ypos = y1;
                edge_zpos = z1;
                edge_irq = 1;
#if 0   /* XXX  uncertain whether point is displayed during edge intr. */
                return;                 /* point not displayed */
#endif
            } else
                edge_flag = 0;
        }
    }
    if (i && ONSCREEN(x1, y1)) {
        if (menu)
            illum3(x1 + MENU_OFFSET, y1, z1);
        else
            illum3(x1, y1, z1);
    }
}

#define point2(i,x,y,e) point3(i, x, y, zpos, e)
                        /* the extra overhead if not depth cueing is not much */

/* 4 bit counter, fed from div 2 clock (to compensate for raster algorithm) */
/* XXX  check display against example photos to see if div 2 is right */
static unsigned char line_counter;
#define LC1 02
#define LC2 04
#define LC3 010
#define LC4 020

/* point on a line (apply line style) */
static void
lpoint(int32 x, int32 y, int32 z)
                        /* X, Y are in window-system screen pixel units */
                        /* Z is in virtual CRT units (offset and normalized) */
{
    int i, on = (line_type == SOLID) || stroking;       /* on for sure */

    if (!on) {                          /* see if in visible portion of cycle */
        for (i = 0; i < reduce; ++i) {
            switch (line_type) {
            case LONG_DASH:
                if (line_counter & LC4)
                    on = 1;
                break;
            case SHORT_DASH:
                if (line_counter & LC3)
                    on = 1;
                break;
            case DOT_DASH:
                /* LC(2:1)H * LC3L + LC4L */
                if (((line_counter & (LC1|LC2)) == (LC1|LC2)
                        && !(line_counter & LC3))  ||  !(line_counter & LC4))
                    on = 1;
                break;
            case SOLID:
                break;
            }

        --line_counter;
        }
    }

    if (on)
        /* convert back from actual screen pixels to emulated CRT coordinates */
        /* note: Z coordinate is already in virtual CRT units */
        illum3(x * reduce, y * reduce, z);
}

/*
 * 2-step algorithm, developed by Xiaolin Wu
 * from http://graphics.lcs.mit.edu/~mcmillan/comp136/Lecture6/Lines.html
 *
 * The two-step algorithm takes the interesting approach of treating
 * line drawing as a automaton, or finite state machine.  If one looks
 * at the possible configurations that the next two pixels of a line,
 * it is easy to see that only a finite set of possiblities exist.
 * If line styles weren't involved, the line could be drawn symmetrically
 * from both ends toward the midpoint.
 * Rasterization is done using actual screen pixel units, not emulated device
 * coordinates!
 *
 * The Z coordinate just goes along for the ride.  It is computed thusly:
 *      Let N = # steps in major direction (X or Y)
 *          i = step number
 *         dZ = Z1 - Z0
 *      Then Zi = floor(Z0 + dZ*(i+0.5)/N)      0.5 centers steps
 *           Zi = floor((2*N*Z0 + dZ + 2*i*dZ) / (2*N))
 *      The numerator at step i is
 *           Znum(i) = Znum(i-1) + 2*dZ
 *      with Znum(0) = 2*N*Z0 + dZ
 */

static void
lineTwoStep(int32 x0, int32 y0, int32 z0, int32 x1, int32 y1, int32 z1)
                                /* virtual CRT units (offset and normalized) */
{
    int32 dx, dy, dz;
    int stepx, stepy;

    /* when clipping is implemented, coords should always be on-screen */

    /* convert from emulated CRT units to actual screen pixels */
    x0 /= reduce;
    y0 /= reduce;
    x1 /= reduce;
    y1 /= reduce;
    /* note: Z coords remain in virtual CRT units */

    dx = x1 - x0;
    dy = y1 - y0;
    dz = z1 - z0;

    /* XXX  there could be fast special cases for "basic vectors" */

    if (dx >= 0)
        stepx = 1;
    else {
        dx = -dx;
        stepx = -1;
    }
    if (dy >= 0)
        stepy = 1;
    else {
        dy = -dy;
        stepy = -1;
    }

#define TPOINT do { znum += dz; /* 2 * original_dz */ \
                    z0 = znum / twoN;   /* truncates */ \
                    if (lphit_irq && !stroking) goto hit; \
                    /* XXX  longjmp from hit detector may be more efficient */ \
                    lpoint(x0, y0, z0); \
               } while (0)

    if (!skip_start)    /* not for continuing stroke when VS60 char. or arc */
        lpoint(x0, y0, z0);             /* (could have used TPOINT) */

    if (dx == 0 && dy == 0)             /* following algorithm won't work */
        return;                         /* just the one dot */
        /* XXX  not accurate for vector in Z direction */

    if (dx > dy) {
        int32 length = (dx - 1) / 2;
        int extras = (dx - 1) & 1;
        int32 incr2 = (dy * 4) - (dx * 2);
        long twoN = 2 * dx, znum = twoN * z0 + dz;
        dz *= 2;
        if (incr2 < 0) {
            int32 c = dy * 2;
            int32 incr1 = c * 2;
            int32 d =  incr1 - dx;
            int32 i;
            for (i = 0; i < length; i++) {
                x0 += stepx;
                if (d < 0) {                            /* Pattern: */
                    TPOINT;                             /*  x o o */
                    x0 += stepx;
                    TPOINT;
                    d += incr1;
                }
                else {
                    if (d < c) {                        /* Pattern: */
                        TPOINT;                         /*      o   */
                        y0 += stepy;                    /*  x o     */
                    } else {                            /* Pattern: */
                        y0 += stepy;                    /*    o o   */
                        TPOINT;                         /*  x       */
                    }
                    x0 += stepx;
                    TPOINT;
                    d += incr2;
                }
            }
            if (extras > 0) {
                x0 += stepx;
                if (d >= c)
                    y0 += stepy;
                TPOINT;
            }

        } else {
            int32 c = (dy - dx) * 2;    /* negative */
            int32 incr1 = c * 2;        /* negative */
            int32 d =  incr1 + dx;
            int32 i;
            for (i = 0; i < length; i++) {
                x0 += stepx;
                if (d > 0) {                            /* Pattern: */
                    y0 += stepy;                        /*      o   */
                    TPOINT;                             /*    o     */
                    x0 += stepx;                        /*  x       */
                    y0 += stepy;
                    TPOINT;
                    d += incr1;
                } else {
                    if (d < c) {                        /* Pattern: */
                        TPOINT;                         /*      o   */
                        y0 += stepy;                    /*  x o     */
                    } else {                            /* Pattern: */
                        y0 += stepy;                    /*    o o   */
                        TPOINT;                         /*  x       */
                    }
                    x0 += stepx;
                    TPOINT;
                    d += incr2;
                }
            }
            if (extras > 0) {
                x0 += stepx;
                if (d >= c)
                    y0 += stepy;
                TPOINT;
            }
        }

    } else {                            /* dy >= dx */
        int32 length = (dy - 1) / 2;
        int extras = (dy - 1) & 1;
        int32 incr2 = (dx * 4) - (dy * 2);
        long twoN = 2 * dy, znum = twoN * z0 + dz;
        dz *= 2;
        if (incr2 < 0) {
            int32 c = dx * 2;
            int32 incr1 = c * 2;
            int32 d =  incr1 - dy;
            int32 i;
            for (i = 0; i < length; i++) {
                y0 += stepy;
                if (d < 0) {                            /* Pattern: */
                    TPOINT;                             /*  o       */
                    y0 += stepy;                        /*  o       */
                    TPOINT;                             /*  x       */
                    d += incr1;
                } else {
                    if (d < c) {                        /* Pattern: */
                        TPOINT;                         /*    o     */
                        x0 += stepx;                    /*  o       */
                                                        /*  x       */
                    } else {                            /* Pattern: */
                        x0 += stepx;                    /*    o     */
                        TPOINT;                         /*    o     */
                                                        /*  x       */
                    }
                    y0 += stepy;
                    TPOINT;
                    d += incr2;
                }
            }
            if (extras > 0) {
                y0 += stepy;
                if (d >= c)
                    x0 += stepx;
                TPOINT;
            }

        } else {
            int32 c = (dx - dy) * 2;    /* nonpositive */
            int32 incr1 = c * 2;        /* nonpositive */
            int32 d =  incr1 + dy;
            int32 i;
            for (i = 0; i < length; i++) {
                y0 += stepy;
                if (d > 0) {                            /* Pattern: */
                    x0 += stepx;
                    TPOINT;                             /*      o   */
                    y0 += stepy;                        /*    o     */
                    x0 += stepx;                        /*  x       */
                    TPOINT;
                    d += incr1;
                } else {
                    if (d < c) {                        /* Pattern: */
                        TPOINT;                         /*    o     */
                        x0 += stepx;                    /*  o       */
                                                        /*  x       */
                    } else {                            /* Pattern: */
                        x0 += stepx;                    /*    o     */
                        TPOINT;                         /*    o     */
                                                        /*  x       */
                    }
                    y0 += stepy;
                    TPOINT;
                    d += incr2;
                }
            }
            if (extras > 0) {
                y0 += stepy;
                if (d >= c)
                    x0 += stepx;
                TPOINT;
            }
        }
    }
    lpoint(x1, y1, z1);         /* not TPOINT (0-length vector on resume) */
    return;

    /* here if LP hit interrupt during rendering */
  hit:
    more_vect = 1;
    save_x0 = x0 * reduce;
    save_y0 = y0 * reduce;
    save_z0 = z0;
    save_x1 = x1 * reduce;
    save_y1 = y1 * reduce;
    save_z1 = z1;
    /* line_counter is static and thus will be intact upon resume */
} /* lineTwoStep */

/*
 * Clip segment to only that portion, if any, visible within the window.
 * Returns:     -1      visible and not clipped
 *               0      invisible
 *               1,2,3  visible and clipped (clipped coords stashed);
 *                      bit-coded:  1 => entry clipped, 2=> exit clipped
 *
 * The Z coordinate just goes along for the ride.
 */
int
clip3(int32 x0, int32 y0, int32 z0, int32 x1, int32 y1, int32 z1)
{
    int code0, code1;                   /* Cohen-Sutherland endpoint codes */
    /* remaining variables are used in modified Liang-Barsky algorithm: */
    int32 rdx, rdy, rdz;                /* x0-x1, y0-y1, z0-z1 */
    int32 tn;                           /* Edge parameter: numerator */
    int32 tPEn, tPEd, tPLn, tPLd;       /* Enter/Leave params: numer, denom */
    int clipped;                        /* potential clip_vect value */

    /*
     * Use the first parts of the Cohen-Sutherland algorithm to detect
     * all IN-to-IN cases and OUT-to-OUT along the same side, each of
     * which is trivially handled without needing any clipping actions.

     * The idea is that the extended window edges divide the plane into
     * 9 regions; the segment endpoints are assigned bit-codes that
     * indicate which of the 3 X sections and which of the 3 Y sections
     * each point lies in; then simple tests on the codes can detect
     * the desired "trivial" cases, which are the most common.
     */

    /* assign X/Y region codes to the endpoints */

    if (y0 > CLIPYMAX)
        code0 = 1;
    else if (y0 < 0)
        code0 = 2;
    else
        code0 = 0;

    if (x0 > CLIPXMAX)
        code0 |= 4;
    else if (x0 < 0)
        code0 |= 8;

    if (y1 > CLIPYMAX)
        code1 = 1;
    else if ( y1 < 0 )
        code1 = 2;
    else
        code1 = 0;

    if (x1 > CLIPXMAX)
        code1 |= 4;
    else if ( x1 < 0 )
        code1 |= 8;

    if (code0 == code1) {               /* endpoints lie in same region */
        if (code0 == 0)                 /* ON to ON; trivially visible */
            return -1;
        else                            /* OFF to OFF and trivially invisible */
            return 0;
    }

    /* Endpoints are now known to lie in different regions. */

    if ((code0 & code1) != 0)           /* OFF to OFF and trivially invisible */
        return 0;

    /* Handle horizontal and vertical cases separately,
       both for speed and to simplify later computations. */

    rdx = x0 - x1;
    rdy = y0 - y1;
    rdz = z0 - z1;

    if (rdx == 0) {                     /* vertical; has a visible portion! */
        clipped = 0;
        /* Using the direction allows us to save one test. */
        if (rdy < 0) {                  /* directed upward */
            if (y1 > CLIPYMAX) {
                clipped = 2;
                y1 = CLIPYMAX;          /* clip */
                z1 = z0 + rdz * (y1 - y0) / rdy;
            }
            if (y0 < 0) {
                clipped |= 1;
                z0 -= rdz * y0 / rdy;
                y0 = 0;                 /* clip */
            }
        } else {                                /* directed downward */
            if (y0 > CLIPYMAX) {
                clipped = 1;
                y0 = CLIPYMAX;          /* clip */
                z0 = z1 + rdz * (y0 - y1) / rdy;
            }
            if (y1 < 0) {
                clipped |= 2;
                z1 -= rdz * y1 / rdy;
                y1 = 0;                 /* clip */
            }
        }
        goto stash;
    }

    if (rdy == 0) {                     /* horizontal; has a visible portion! */
        clipped = 0;
        /* Using the direction allows us to save one test. */
        if (rdx < 0) {                  /* directed rightward */
            if (x1 > CLIPXMAX) {
                clipped |= 2;
                x1 = CLIPXMAX;          /* clip */
                z1 = z0 + rdz * (x1 - x0) / rdx;
            }
            if (x0 < 0) {
                clipped = 1;
                z0 -= rdz * x0 / rdx;
                x0 = 0;                 /* clip */
            }
        } else {                                /* directed leftward */
            if (x0 > CLIPXMAX) {
                clipped = 1;
                x0 = CLIPXMAX;          /* clip */
                z0 = z1 + rdz * (x0 - x1) / rdx;
            }
            if (x1 < 0) {
                clipped |= 2;
                z1 -= rdz * x1 / rdx;
                x1 = 0;                 /* clip */
            }
        }
        goto stash;
    }

    /*
     * Hardest cases: use modified Liang-Barsky algorithm.
     *
     * Not only is this computation supposedly faster than Cohen-
     * Sutherland clipping, but also the original direction is
     * preserved, which is necessary to accurately emulate the
     * VT48 behavior (association of coordinates with interrupts).
     */

    /*
     * t is a line parameter:   P(t) = P0 + t * (P1 - P0).
     * N is an outward normal vector.
     * L, R, B, T denote edges (left, right, bottom, top).
     * PE denotes "potentially entering", PL "potentially leaving".
     * n, d denote numerator, denominator (avoids floating point).
     */

    /*
     * We know at this point that the endpoints lie in different
     * regions and that there must be at least one PE or PL crossing
     * at some value of t in [0,1].  Indeed, there will be *both* PE
     * and PL crossings *unless* one endpoint is IN the window.
     *
     * As a result of the previous filtering, denominators are never 0.
     */

    tPEn = -1;          /* tPE = -1, lower than any candidate */
    tPEd = 1;
    tPLn = 2;           /* tPL = 2, higher than any candidate */
    tPLd = 1;

    /*
     * Left:    tL = NL . (PL - P0) / NL . (P1 - P0)
     *          NL = (-1,0)
     *          PL = (0,y)
     * =>
     *          tL = x0 / rdx
     *
     *  if ( tL >= 0 & tL <= 1 )
     *          if ( NL . (P1 - P0) < 0 & tL > tPE )
     *                  tPE := tL
     *          if ( NL . (P1 - P0) > 0 & tL < tPL )
     *                  tPL := tL
     * =>
     *  if ( rdx < 0 )
     *          if ( rdx <= x0 & x0 <= 0 )
     *                  if ( tPEd > 0 )
     *                          if ( x0 * tPEd < tPEn * rdx )
     *                                  tPE := tL
     *                  else
     *                          if ( x0 * tPEd > tPEn * rdx )
     *                                  tPE := tL
     *  else
     *          if ( 0 <= x0 & x0 <= rdx )
     *                  if ( tPLd > 0 )
     *                          if ( x0 * tPLd < tPLn * rdx )
     *                                  tPL := tL
     *                  else
     *                          if ( x0 * tPLd > tPLn * rdx )
     *                                  tPL := tL
     */

    if (rdx < 0) {
        if (x0 <= 0 && x0 >= rdx) {
            if (tPEd > 0) {
                if (x0 * (long)tPEd < (long)tPEn * rdx)
                    tPEn = x0, tPEd = rdx;
            } else                      /* tPEd < 0 */
                if (x0 * (long)tPEd > (long)tPEn * rdx)
                    tPEn = x0, tPEd = rdx;
        }
    } else {                            /* rdx > 0 */
        if (x0 >= 0 && x0 <= rdx) {
            if (tPLd > 0) {
                if (x0 * (long)tPLd < (long)tPLn * rdx)
                    tPLn = x0, tPLd = rdx;
            } else                      /* tPLd < 0 */
                if (x0 * (long)tPLd > (long)tPLn * rdx)
                    tPLn = x0, tPLd = rdx;
        }
    }

    /*
     * Right:   tR = NR . (PR - P0) / NR . (P1 - P0)
     *          NR = (1,0)
     *          PR = (XMAX,y)
     * =>
     *          tR = (x0 - XMAX) / rdx
     *
     *  if ( tR >= 0 & tR <= 1 )
     *          if ( NR . (P1 - P0) < 0 & tR > tPE )
     *                  tPE := tR
     *          if ( NR . (P1 - P0) > 0 & tR < tPL )
     *                  tPL := tR
     * =>
     *  if ( rdx < 0 )
     *          if ( rdx <= TRn & TRn <= 0 )
     *                  if ( tPLd > 0 )
     *                          if ( TRn * tPLd > tPLn * rdx )
     *                                  tPL := tR
     *                  else
     *                          if ( TRn * tPLd < tPLn * rdx )
     *                                  tPL := tR
     *  else
     *          if ( 0 <= TRn & TRn <= rdx )
     *                  if ( tPEd > 0 )
     *                          if ( TRn * tPEd > tPEn * rdx )
     *                                  tPE := tR
     *                  else
     *                          if ( TRn * tPEd < tPEn * rdx )
     *                                  tPE := tR
     */

    tn = x0 - CLIPXMAX;

    if (rdx < 0) {
        if (tn <= 0 && tn >= rdx) {
            if (tPLd > 0) {
                if (tn * (long)tPLd > (long)tPLn * rdx)
                    tPLn = tn, tPLd = rdx;
            } else                      /* tPLd < 0 */
                if (tn * (long)tPLd < (long)tPLn * rdx)
                    tPLn = tn, tPLd = rdx;
        }
    } else {                            /* rdx > 0 */
        if (tn >= 0 && tn <= rdx) {
            if (tPEd > 0) {
                if (tn * (long)tPEd > (long)tPEn * rdx)
                    tPEn = tn, tPEd = rdx;
            } else                      /* tPEd < 0 */
                if (tn * (long)tPEd < (long)tPEn * rdx)
                    tPEn = tn, tPEd = rdx;
        }
    }

    /*
     * Bottom:  tB = NB . (PB - P0) / NB . (P1 - P0)
     *          NB = (0,-1)
     *          PB = (x,0)
     * =>
     *          tB = y0 / rdy
     *
     *  if ( tB >= 0 & tB <= 1 )
     *          if ( NB . (P1 - P0) < 0 & tB > tPE )
     *                  tPE := tB
     *          if ( NB . (P1 - P0) > 0 & tB < tPL )
     *                  tPL := tB
     * =>
     *  if ( rdy < 0 )
     *          if ( rdy <= y0 & y0 <= 0 )
     *                  if ( tPEd > 0 )
     *                          if ( y0 * tPEd < tPEn * rdy )
     *                                  tPE := tB
     *                  else
     *                          if ( y0 * tPEd > tPEn * rdy )
     *                                  tPE := tB
     *  else
     *          if ( 0 <= y0 & y0 <= rdy )
     *                  if ( tPLd > 0 )
     *                          if ( y0 * tPLd < tPLn * rdy )
     *                                  tPL := tB
     *                  else
     *                          if ( y0 * tPLd > tPLn * rdy )
     *                                  tPL := tB
     */

    if (rdy < 0) {
        if (y0 <= 0 && y0 >= rdy) {
            if (tPEd > 0) {
                if (y0 * (long)tPEd < (long)tPEn * rdy)
                    tPEn = y0, tPEd = rdy;
            } else                      /* tPEd < 0 */
                if (y0 * (long)tPEd > (long)tPEn * rdy)
                    tPEn = y0, tPEd = rdy;
        }
    } else                              /* rdy > 0 */
        if (y0 >= 0 && y0 <= rdy) {
            if (tPLd > 0) {
                if (y0 * (long)tPLd < (long)tPLn * rdy)
                    tPLn = y0, tPLd = rdy;
            } else {                    /* tPLd < 0 */
                if (y0 * (long)tPLd > (long)tPLn * rdy)
                    tPLn = y0, tPLd = rdy;
            }
        }

    /*
     * Top:     tT = NT . (PT - P0) / NT . (P1 - P0)
     *          NT = (0,1)
     *          PT = (x,YMAX)
     * =>
     *          tT = (y0 - YMAX) / rdy
     *
     *  if ( tT >= 0 & tT <= 1 )
     *          if ( NT . (P1 - P0) < 0 & tT > tPE )
     *                  tPE := tT
     *          if ( NT . (P1 - P0) > 0 & tT < tPL )
     *                  tPL := tT
     * =>
     *  if ( rdy < 0 )
     *          if ( rdy <= TRn & TRn <= 0 )
     *                  if ( tPLd > 0 )
     *                          if ( TRn * tPLd > tPLn * rdy )
     *                                  tPL := tT
     *                  else
     *                          if ( TRn * tPLd < tPLn * rdy )
     *                                  tPL := tT
     *  else
     *          if ( 0 <= TRn & TRn <= rdy )
     *                  if ( tPEd > 0 )
     *                          if ( TRn * tPEd > tPEn * rdy )
     *                                  tPE := tT
     *                  else
     *                          if ( TRn * tPEd < tPEn * rdy )
     *                                  tPE := tT
     */

    tn = y0 - CLIPYMAX;

    if (rdy < 0) {
        if (tn <= 0 && tn >= rdy) {
            if (tPLd > 0) {
                if (tn * (long)tPLd > (long)tPLn * rdy)
                    tPLn = tn, tPLd = rdy;
            } else                      /* tPLd < 0 */
                if (tn * (long)tPLd < (long)tPLn * rdy)
                    tPLn = tn, tPLd = rdy;
        }
    } else {                            /* rdy > 0 */
        if (tn >= 0 && tn <= rdy) {
            if (tPEd > 0) {
                if (tn * (long)tPEd > (long)tPEn * rdy)
                    tPEn = tn, tPEd = rdy;
            } else                      /* tPEd < 0 */
                if (tn * (long)tPEd < (long)tPEn * rdy)
                    tPEn = tn, tPEd = rdy;
        }
    }

    /*
     *  if ( tPL < tPE )
     *          invisible
     * =>
     *  if ( tPLd > 0 && tPEd < 0 || tPLd < 0 && tPEd > 0 )
     *          if ( tPLn * tPEd > tPEn * tPLd )
     *                  invis
     *  else
     *          if ( tPLn * tPEd < tPEn * tPLd )
     *                  invis
     */

    if (((tPLd > 0) && (tPEd < 0)) || 
        ((tPLd < 0) && (tPEd > 0))) {
        if (tPLn * (long)tPEd > (long)tPEn * tPLd)
            return 0;                   /* invisible */
    } else
        if (tPLn * (long)tPEd < (long)tPEn * tPLd)
            return 0;                   /* invisible */

    /*
     *  if ( tPE < 0 ) tPE := 0     [code0 is 0]
     *  if ( tPL > 1 ) tPL := 1     [code1 is 0]
     *  draw from P(tPE) to P(tPL)
     *
     *  P(t) = P0 + t * (P1 - P0)
     * =>
     *  xE = x0 - tE * rdx, yE = y0 - tE * rdy
     *  xL = x0 - tL * rdx, yL = y0 - tL * rdy
     */

    /* note: update P1 first since it uses original P0 coords */

    if (code1 == 0)
        clipped = 0;
    else {
        clipped = 2;
        /* XXX  might not be rounded the same as on the VT48: */
        x1 = x0 - rdx * tPLn / tPLd;
        y1 = y0 - rdy * tPLn / tPLd;
        z1 = z0 - rdz * tPLn / tPLd;
    }

    if (code0 != 0) {
        clipped |= 1;
        /* XXX  might not be rounded the same as on the VT48: */
        x0 -= rdx * tPEn / tPEd;
        y0 -= rdy * tPEn / tPEd;
        z0 -= rdz * tPEn / tPEd;
    }

    /* Stash clipped coords and set global "vector was clipped" flag. */

  stash:
    clip_x0 = x0;
    clip_y0 = y0;
    clip_x1 = x1;
    clip_y1 = y1;
    clip_z0 = z0;
    clip_z1 = z1;
    return clipped;
}

/* draw a relative vector, depth-cued when appropriate */

static void
vector3(int i, int32 dx, int32 dy, int32 dz)   /* unscaled display-file units */
{
    int32 x0, y0, z0, x1, y1, z1;

    dx = stroking ? CSCALE(dx) : VSCALE(dx);    /* apply scale factor (VS60) */
    dy = stroking ? CSCALE(dy) : VSCALE(dy);
    dz = VSCALE(dz * 4);
    x0 = PNORM(xpos);                   /* (includes offset) */
    y0 = PNORM(ypos);
    z0 = PNORM(zpos);
    xpos += dx;
    ypos += dy;
    zpos += dz;
    x1 = PNORM(xpos);
    y1 = PNORM(ypos);
    z1 = PNORM(zpos);
    dx = x1 - x0;
    dy = y1 - y0;
    dz = z1 - z0;

    if (stroking) {                     /* drawing a VS60 character */
        DEBUGF("offset, normalized stroke i%d (%ld,%ld) to (%ld,%ld)\r\n",
               i, (long)x0,(long)y0, (long)x1,(long)y1);

        if (dx == 0 && dy == 0) {       /* just display a point */
            if (i) {
                if (menu)
                    illum3(x0 + MENU_OFFSET, y0, z0);
                else
                    illum3(x0, y0, z0); /* illum3() checks ONCRT, int0_scope */
            }
            return;
        }
    } else {
        DEBUGF("offset, normalized vector i%d (%ld,%ld,%ld) to (%ld,%ld,%ld)\r\n",
               i, (long)x0, (long)y0, (long)z0, (long)x1, (long)y1, (long)z1);

        line_counter = 037;             /* reset line-style counter */

        /* Maintenance Switch 3 => store delta length,tangent in xpos,ypos */
        if (maint3) {
            int32 adx = ABS(dx), ady = ABS(dy);
            if (adx == ady) {
                xpos = 07777;           /* ~ 1.0 */
                ypos = adx;             /* or ady */
            } else if (adx > ady) {
                xpos = adx;
                ypos = 010000L * ady / adx + 1; /* truncates */
            } else /* (adx < ady) */ {
                xpos = 010000L * adx / ady + 1; /* truncates */
                ypos = ady;             /* according to DZVSC test 100 */
            }
            DEBUGF("delta=0%o, tangent=0%o\r\n", xpos, ypos);
            xpos = PSCALE(xpos);        /* compensates for eventual PNORM */
            ypos = PSCALE(ypos);        /* compensates for eventual PNORM */
        }

        /* clip to viewport ("working surface") if necessary */

        /*
         * Note about edge conditions and interrupts:
         *
         * The VT48 documentation isn't very clear about this, but the expected
         * behavior has been determined from one of the VS60 diagnostics.  The
         * "edge flag" flip-flop (bit) corresponds directly to an edge interrupt
         * (controlled by the "edge interrupt enable" bit in a Load Status BB
         * instruction) and is set precisely twice for *each* vector that is
         * clipped in *any* way (on->off, off->off, off->on), assuming that
         * after each interrupt is caught a RESUME (set DPC with odd value) is
         * issued.  The X,Y position registers at the time of the first edge
         * interrupt for a clipped vector give the starting position of the
         * *visible* segment; the position registers at the time of the second
         * edge interrupt for a clipped vector give the ending position of the
         * *visible* segment.  The "edge indicator" flip-flop (bit) at the time
         * of an edge interrupt is set if and only if the vector has been
         * clipped at that position.  Thus for on-to-off, the edge indicator is
         * set for just the second edge interrupt; for off-to-off, the edge
         * indicator is set for both edge interrupts; for off-to-on, the edge
         * indicator is set for just the first interrupt.  Resuming after a
         * vector has gone off-screen updates the position registers to the
         * location (off-screen) specified in the display file.  Edge interrupts
         * share an interrupt vector with other "surface" interrupts such as
         * light-pen hits.
         *
         * It appears from diagnostic DZVSD that the menu area might not be
         * clipped.
         *
         * Note that the VT11 cannot generate edge interrupts, and its edge
         * indicator provides less information than on the VS60.
         */

        switch (clip_vect = clip3(x0, y0, z0, x1, y1, z1)) {
        case 1:                         /* clipped only on entry */
        case 3:                         /* clipped on entry and exit */
            edge_indic = 1;             /* indicate clipped going in */
                                        /* XXX  might not be correct for VT11 */
        case 2:                         /* clipped only on exit */
            edge_flag = edge_intr_ena;  /* indicate vector-clip interrupt */
            if (edge_flag) {
                edge_xpos = clip_x0;
                edge_ypos = clip_y0;
                edge_zpos = clip_z0;
                edge_irq = 1;
            }
            clip_i = i;
            return;                     /* may be drawn later by vt_cycle() */
        case 0:                         /* invisible */
            return;
        default:
            DEBUGF("clip() bad return: %d\n", clip_vect);
        case -1:                        /* visible, not clipped */
            clip_vect = 0;
            break;                      /* draw immediately */
        }
    }

    if (dx == 0 && dy == 0 && dz == 0)
        return;                         /* hardware skips null vector */

    /* for character strokes, resort to scissoring:
       illum3() illuminates only pixels that lie in the visible display area */

    /* draw OK even when Maintenance Switch 3 is set */
    /* (but updated position registers must not be used to draw vector) */
    if (i && int0_scope && !clip_vect) {/* clipped vector drawn by vt_cycle() */
        if (menu)
            lineTwoStep(x0 + MENU_OFFSET, y0, z0, x1 + MENU_OFFSET, y1, z1);
        else
            lineTwoStep(x0, y0, z0, x1, y1, z1);
    }

    /*
     * In case of LP hit, recompute coords using "tangent register", because:
     *  (1) distinct virtual CRT points can be mapped into the same pixel
     *  (2) raster computation might not match that of the actual VT48
     */

    if (lp0_hit) {
        long tangent;
        int32 adx = ABS(dx), ady = ABS(dy);
        if (adx >= ady) {
            tangent = 010000L * dy / dx;        /* signed */
            lp_ypos = y0 + tangent * (lp_xpos - x0) / 010000L;
            tangent = 010000L * dz / dx;
            lp_zpos = z0 + tangent * (lp_xpos - x0) / 010000L;
        } else {
            tangent = 010000L * dx / dy;        /* signed */
            lp_xpos = x0 + tangent * (lp_ypos - y0) / 010000L;
            tangent = 010000L * dz / dy;
            lp_zpos = z0 + tangent * (lp_ypos - y0) / 010000L;
        }
        DEBUGF("adjusted LP coords (0%o,0%o,0%o)\r\n",
               lp_xpos, lp_ypos, lp_zpos);
        /* xpos,ypos,zpos still pertain to the original endpoint
           (assuming that Maintenance Switch 3 isn't set) */
    }
}

#define vector2(i,dx,dy) vector3(i,dx,dy,0)
                        /* the extra overhead for Z computation is not much */

/* basic vector (multiple of 45 degrees; directions numbered CCW, #0 => +X) */
static void
basic_vector(int i, int dir, int len)   /* unscaled display-file units */
{
    int32 dx, dy;

    /* Alternatively, could be rasterized specially for each case; then
       the general vector2() function could detect these special cases and
       invoke this function to handle them, instead of the other way around. */

    switch (dir) {
    case 0:
        dx = len;
        dy = 0;
        break;
    case 1:
        dx = len;
        dy = len;
        break;
    case 2:
        dx = 0;
        dy = len;
        break;
    case 3:
        dx = -len;
        dy = len;
        break;
    case 4:
        dx = -len;
        dy = 0;
        break;
    case 5:
        dx = -len;
        dy = -len;
        break;
    case 6:
        dx = 0;
        dy = -len;
        break;
    case 7:
        dx = len;
        dy = -len;
        break;
    default:                            /* "can't happen" */
        DEBUGF("BUG: basic vector: illegal direction %d\r\n", dir);
        return;
    }
    DEBUGF("basic ");
    vector2(i, dx, dy);
}

/*
 * support for VS60 circle/arc option
 *
 * Since the literature that I have access to does not handle the case where
 * starting and ending radii differ, I invented a solution that should be
 * "good enough" for now: an approximation of an Archimedean spiral is drawn
 * as connected individual chords, with the line-type counter applied (without
 * being reset) over the entire curve.
 *
 * It is not known whether the direction is supposed to be clockwise or
 * counterclockwise (the latter is assumed in the following code); it is
 * assumed that if the starting and ending directions from the center point
 * are identical, that a full circle is being specified.
 *
 * Although throughout the display simulation substantial effort has been
 * invested to avoid using floating point, this preliminary implementation
 * of the circle/arc generator does use floating point.  Presumably this
 * is avoidable, but the algorithmic details would need to be worked out.
 * If use of floating point is a problem, #define NO_CONIC_OPT when compiling.
 *
 * The Z coordinate is linearly interpolated.
 */

static void
conic3(int i, int32 dcx, int32 dcy, int32 dcz, int32 dex, int32 dey, int32 dez)
                                        /* unscaled display-file units */
{
#ifdef NO_CONIC_OPT
    /* just draw vector to endpoint (like real VS60 with option missing) */
    vector3(i, dex, dey, dez);
#else
    int32 xs, ys, zs, xc, yc, zc, xe, ye, ze, x, y, z, nseg, seg;
    double rs, re, dr, as, da, zo, dz;
    int ons, one;                       /* ONSCREEN(xs,ys), ONSCREEN(xe,ye) */
    static double two_pi = -1.0;        /* will be set (once only) to 2*Pi */
    static double k;                    /* will be set to 2-sqrt(4-(Pi/4)^2) */

    if (two_pi < 0.0) {                 /* (initial entry only) */
        k = atan2(1.0, 1.0);
        two_pi = 8.0 * k;
        k = 2.0 - sqrt(4.0 - k*k);
    }
    dcx = VSCALE(dcx);                  /* apply vector scale factor */
    dcy = VSCALE(dcy);
    dcz = VSCALE(dcz * 4);
    dex = VSCALE(dex);
    dey = VSCALE(dey);
    dez = VSCALE(dez * 4);
    xs = PNORM(xpos);                   /* starting pos. (includes offset) */
    ys = PNORM(ypos);
    zs = PNORM(zpos);
    xc = PNORM(xpos + dcx);             /* center pos. (includes offset) */
    yc = PNORM(ypos + dcy);
    zc = PNORM(zpos + dcz);
    xe = PNORM(xpos + dex);             /* ending pos. (includes offset) */
    ye = PNORM(ypos + dey);
    ze = PNORM(zpos + dez);
    /* determine vector from center to finish */
    dex -= dcx;                         /* PSCALEd */
    dey -= dcy;
    dez -= dcz;

    DEBUGF("offset, normalized arc i%d s(%ld,%ld,%ld) c(%ld,%ld,%ld) e(%ld,%ld,%ld)\r\n",
           i, (long)xs,(long)ys,(long)zs, (long)xc,(long)yc,(long)zc,
           (long)xe,(long)ye,(long)ze);

    /* XXX  not known whether Maintenance Switch 3 has any effect for arcs */

    /* clip to viewport ("working surface") if necessary */

    /* XXX  not implemented yet [could check each chord individually] */

    /* check for edge conditions (XXX change when conic clipping implemented) */
    /* XXX  this test is very crude; should be much more complex */
    ons = ONSCREEN(xs, ys);
    one = ONSCREEN(xe, ye);
    edge_indic = ons && !one;
    edge_flag  = edge_indic || (!ons && one);
    if (edge_flag) {
        if (edge_intr_ena) {            /* need to clip to viewport */
            /* XXX  edge positions aren't right; need proper clipping */
            edge_xpos = xe;
            edge_ypos = ye;
            edge_zpos = ze;
            edge_irq = 1;
            goto done;
        } else
            edge_flag = 0;
    }
    /* XXX  for now, resort to scissoring:
                illuminates only pixels that lie in the visible display area */

    if (dcx == 0 && dcy == 0 && dcz == 0 && dex == 0 && dey == 0 && dez == 0)
        goto done;                      /* skip null curve */

    /* determine starting, ending radii and their maximum */
    rs = PNORM(sqrt((double)dcx*dcx + (double)dcy*dcy));        /* (f.p.) */
    re = PNORM(sqrt((double)dex*dex + (double)dey*dey));
    dr = rs >= re ? rs : re;

    /* determine starting direction from center, and included angle */
    as = dcx == 0 && dcy == 0 ? 0.0 : atan2((double)-dcy, (double)-dcx);
    da = (dex == 0 && dey == 0 ? 0.0 : atan2((double)dey, (double)dex)) - as;
    while (da <= 0.0)                   /* exactly 0.0 implies full cycle */
        da += two_pi;

    /* determine number of chords to use;
       make deviation from true curve no more than approximately one pixel */
    dr = reduce / dr;
    if (dr > k)
        dr = k;
    nseg = (int32)(da / sqrt(4.0*dr - dr*dr) + 1.0);
    if (nseg < 1)                       /* "can't happen" */
        nseg = 1;
    else if (nseg > 360)
        nseg = 360;                     /* arbitrarily chosen upper limit */

    /* determine angular, radial, and Z step sizes */
    dr = (re - rs) / nseg;
    da /= nseg;
    dz = (double)(ze - zs) / nseg;

    if (menu) {
        xs += MENU_OFFSET;
        xc += MENU_OFFSET;
        xe += MENU_OFFSET;
    }

    line_counter = 037;                 /* reset line-style counter */

    /* draw successive chords */
    zo = zs;
    for (seg = 0; ++seg < nseg; ) {
        rs += dr;
        as += da;
        re = rs * cos(as);
        x = xc + (re >= 0 ? (int32)(re + 0.5) : -(int32)(-re + 0.5));
        re = rs * sin(as);
        y = yc + (re >= 0 ? (int32)(re + 0.5) : -(int32)(-re + 0.5));
        z = (int32)(zo + seg * dz);     /* truncates */
        lineTwoStep(xs, ys, zs, x, y, z);/* (continuing line style) */
        skip_start = 1;                 /* don't double-illuminate junctions */
        xs = x;
        ys = y;
        zs = z;
        if (lphit_irq)
            goto done;                  /* light-pen hit interrupted drawing */
    }
    lineTwoStep(xs, ys, zs, xe, ye, ze);/* draw final chord to exact endpoint */

  done:
    skip_start = 0;                     /* important! */
    xpos += dcx + dex;                  /* update virtual beam position */
    ypos += dcy + dey;
    zpos += dcz + dez;
    if (lp0_hit) {
        DEBUGF("LP hit on arc at (0%o,0%o,0%o)\r\n",
               lp_xpos, lp_ypos, lp_zpos);
        if (lphit_irq) {
            /* XXX  save parameters for drawing remaining chords */
        }
    }
#endif
}

#define conic2(i,dcx,dcy,dex,dey) conic3(i,dcx,dcy,0,dex,dey,0)
                        /* the extra overhead for Z computation is not much */

/*
 * VT11 character font;
 * 6x8 matrix, not serpentine encoded, decenders supported as in real VT11
 */

static const unsigned char dots[0200][6] = {
    { 0x8f, 0x50, 0x20, 0x10, 0x08, 0x07 },     /* 000 lambda */
    { 0x1e, 0x21, 0x22, 0x14, 0x0c, 0x13 },     /* 001 alpha */
    { 0x00, 0x18, 0x24, 0xff, 0x24, 0x18 },     /* 002 phi */
    { 0x83, 0xc5, 0xa9, 0x91, 0x81, 0xc3 },     /* 003 SIGMA */
    { 0x00, 0x46, 0xa9, 0x91, 0x89, 0x06 },     /* 004 delta */
    { 0x03, 0x05, 0x09, 0x11, 0x21, 0x7f },     /* 005 DELTA */
    { 0x00, 0x20, 0x20, 0x3f, 0x01, 0x01 },     /* 006 iota */
    { 0x46, 0x29, 0x11, 0x2e, 0x40, 0x80 },     /* 007 gamma */
    { 0x7f, 0x80, 0x80, 0x80, 0x80, 0x7f },     /* 010 intersect */
    { 0x40, 0x3c, 0x04, 0xff, 0x04, 0x78 },     /* 011 psi */
    { 0x00, 0x10, 0x10, 0x54, 0x10, 0x10 },     /* 012 divide by */
    { 0x00, 0x60, 0x90, 0x90, 0x60, 0x00 },     /* 013 degree */
    { 0x00, 0x01, 0x00, 0x10, 0x00, 0x01 },     /* 014 therefore */
    { 0x01, 0x02, 0x3c, 0x02, 0x02, 0x3c },     /* 015 mu */
    { 0x11, 0x7f, 0x91, 0x81, 0x41, 0x03 },     /* 016 pound sterling */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },     /* 017 SHIFT IN */
    { 0x20, 0x40, 0x7f, 0x40, 0x7f, 0x40 },     /* 020 pi */
    { 0x00, 0xff, 0x00, 0x00, 0xff, 0x00 },     /* 021 parallel */
    { 0x1d, 0x23, 0x40, 0x42, 0x25, 0x19 },     /* 022 OMEGA */
    { 0x1c, 0x22, 0x61, 0x51, 0x4e, 0x40 },     /* 023 sigma */
    { 0x20, 0x40, 0x40, 0x7f, 0x40, 0x40 },     /* 024 UPSILON */
    { 0x00, 0x1c, 0x2a, 0x49, 0x49, 0x00 },     /* 025 epsilon */
    { 0x10, 0x38, 0x54, 0x10, 0x10, 0x10 },     /* 026 left arrow */
    { 0x10, 0x10, 0x10, 0x54, 0x38, 0x10 },     /* 027 right arrow */
    { 0x00, 0x20, 0x40, 0xfe, 0x40, 0x20 },     /* 030 up arrow */
    { 0x00, 0x04, 0x02, 0x7f, 0x02, 0x04 },     /* 031 down arrow */
    { 0x00, 0xff, 0x80, 0x80, 0x80, 0x80 },     /* 032 GAMMA */
    { 0x00, 0x01, 0x01, 0xff, 0x01, 0x01 },     /* 033 perpendicular */
    { 0x2a, 0x2c, 0x28, 0x38, 0x68, 0xa8 },     /* 034 unequal */
    { 0x24, 0x48, 0x48, 0x24, 0x24, 0x48 },     /* 035 approx equal */
    { 0x00, 0x20, 0x10, 0x08, 0x10, 0x20 },     /* 036 vel */
    { 0xff, 0x81, 0x81, 0x81, 0x81, 0xff },     /* 037 box */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },     /* 040 space */
    { 0x00, 0x00, 0x00, 0xfd, 0x00, 0x00 },     /* 041 ! */
    { 0x00, 0xe0, 0x00, 0x00, 0xe0, 0x00 },     /* 042 " */
    { 0x00, 0x24, 0xff, 0x24, 0xff, 0x24 },     /* 043 # */
    { 0x22, 0x52, 0xff, 0x52, 0x4c, 0x00 },     /* 044 $ */
    { 0x42, 0xa4, 0x48, 0x12, 0x25, 0x42 },     /* 045 % */
    { 0x66, 0x99, 0x99, 0x66, 0x0a, 0x11 },     /* 046 & */
    { 0x00, 0x00, 0x20, 0x40, 0x80, 0x00 },     /* 047 ' */
    { 0x00, 0x00, 0x3c, 0x42, 0x81, 0x00 },     /* 050 ( */
    { 0x00, 0x00, 0x81, 0x42, 0x3c, 0x00 },     /* 051 ) */
    { 0x00, 0x44, 0x28, 0xf0, 0x28, 0x44 },     /* 052 * */
    { 0x00, 0x10, 0x10, 0x7c, 0x10, 0x10 },     /* 053 + */
    { 0x00, 0x01, 0x06, 0x00, 0x00, 0x00 },     /* 054 , */
    { 0x00, 0x10, 0x10, 0x10, 0x10, 0x10 },     /* 055 - */
    { 0x00, 0x00, 0x06, 0x06, 0x00, 0x00 },     /* 056 . */
    { 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 },     /* 057 / */
    { 0x7e, 0x85, 0x89, 0x91, 0xa1, 0x7e },     /* 060 0 */
    { 0x00, 0x41, 0xff, 0x01, 0x00, 0x00 },     /* 061 1 */
    { 0x47, 0x89, 0x91, 0x91, 0x91, 0x61 },     /* 062 2 */
    { 0x42, 0x81, 0x91, 0xb1, 0xd1, 0x8e },     /* 063 3 */
    { 0x0c, 0x14, 0x24, 0x44, 0xff, 0x04 },     /* 064 4 */
    { 0xf2, 0x91, 0x91, 0x91, 0x91, 0x8e },     /* 065 5 */
    { 0x3c, 0x46, 0x89, 0x89, 0x89, 0x46 },     /* 066 6 */
    { 0x40, 0x87, 0x88, 0x90, 0xa0, 0xc0 },     /* 067 7 */
    { 0x6e, 0x91, 0x91, 0x91, 0x91, 0x6e },     /* 070 8 */
    { 0x62, 0x91, 0x91, 0x91, 0x62, 0x3c },     /* 071 9 */
    { 0x00, 0x66, 0x66, 0x00, 0x00, 0x00 },     /* 072 : */
    { 0x00, 0x00, 0x61, 0x66, 0x00, 0x00 },     /* 073 ; */
    { 0x00, 0x18, 0x24, 0x42, 0x81, 0x00 },     /* 074 < */
    { 0x00, 0x28, 0x28, 0x28, 0x28, 0x28 },     /* 075 = */
    { 0x00, 0x81, 0x42, 0x24, 0x18, 0x00 },     /* 076 > */
    { 0x00, 0x40, 0x80, 0x9d, 0x90, 0x60 },     /* 077 ? */
    { 0x3c, 0x42, 0x91, 0xa9, 0xa9, 0x72 },     /* 100 @ */
    { 0x3f, 0x48, 0x88, 0x88, 0x48, 0x3f },     /* 101 A */
    { 0x81, 0xff, 0x91, 0x91, 0x91, 0x6e },     /* 102 B */
    { 0x3c, 0x42, 0x81, 0x81, 0x81, 0x42 },     /* 103 C */
    { 0x81, 0xff, 0x81, 0x81, 0x42, 0x3c },     /* 104 D */
    { 0x81, 0xff, 0x91, 0x91, 0x91, 0xc3 },     /* 105 E */
    { 0x81, 0xff, 0x91, 0x90, 0x80, 0xc0 },     /* 106 F */
    { 0x3c, 0x42, 0x81, 0x89, 0x89, 0x4f },     /* 107 G */
    { 0xff, 0x10, 0x10, 0x10, 0x10, 0xff },     /* 110 H */
    { 0x00, 0x81, 0xff, 0x81, 0x00, 0x00 },     /* 111 I */
    { 0x0e, 0x01, 0x01, 0x81, 0xfe, 0x80 },     /* 112 J */
    { 0xff, 0x08, 0x10, 0x28, 0x44, 0x83 },     /* 113 K */
    { 0x81, 0xff, 0x81, 0x01, 0x01, 0x03 },     /* 114 L */
    { 0xff, 0x40, 0x30, 0x30, 0x40, 0xff },     /* 115 M */
    { 0xff, 0x20, 0x10, 0x08, 0x04, 0xff },     /* 116 N */
    { 0x3c, 0x42, 0x81, 0x81, 0x42, 0x3c },     /* 117 O */
    { 0x81, 0xff, 0x90, 0x90, 0x90, 0x60 },     /* 120 P */
    { 0x3c, 0x42, 0x81, 0x8f, 0x42, 0x3d },     /* 121 Q */
    { 0x81, 0xff, 0x90, 0x98, 0x94, 0x63 },     /* 122 R */
    { 0x22, 0x51, 0x91, 0x91, 0x89, 0x46 },     /* 123 S */
    { 0xc0, 0x80, 0x81, 0xff, 0x81, 0xc0 },     /* 124 T */
    { 0xfe, 0x01, 0x01, 0x01, 0x01, 0xfe },     /* 125 U */
    { 0xff, 0x02, 0x04, 0x08, 0x10, 0xe0 },     /* 126 V */
    { 0xff, 0x02, 0x0c, 0x0c, 0x02, 0xff },     /* 127 W */
    { 0xc3, 0x24, 0x18, 0x18, 0x24, 0xc3 },     /* 130 X */
    { 0x00, 0xe0, 0x10, 0x0f, 0x10, 0xe0 },     /* 131 Y */
    { 0x83, 0x85, 0x89, 0x91, 0xa1, 0xc1 },     /* 132 Z */
    { 0x00, 0x00, 0xff, 0x81, 0x81, 0x00 },     /* 133 [ */
    { 0x00, 0x40, 0x20, 0x10, 0x08, 0x04 },     /* 134 \ */
    { 0x00, 0x00, 0x81, 0x81, 0xff, 0x00 },     /* 135 ] */
    { 0x00, 0x10, 0x20, 0x40, 0x20, 0x10 },     /* 136 ^ */
    { 0x01, 0x01, 0x01, 0x01, 0x01, 0x00 },     /* 137 _ */
    /* for all lowercase characters, first column is just a "descender" flag: */
    { 0x00, 0x00, 0x80, 0x40, 0x20, 0x00 },     /* 140 ` */
    { 0x00, 0x26, 0x29, 0x29, 0x2a, 0x1f },     /* 141 a */
    { 0x00, 0xff, 0x12, 0x21, 0x21, 0x1e },     /* 142 b */
    { 0x00, 0x1e, 0x21, 0x21, 0x21, 0x12 },     /* 143 c */
    { 0x00, 0x1e, 0x21, 0x21, 0x12, 0xff },     /* 144 d */
    { 0x00, 0x1e, 0x29, 0x29, 0x29, 0x19 },     /* 145 e */
    { 0x00, 0x20, 0x7f, 0xa0, 0xa0, 0x80 },     /* 146 f */
    { 0x01, 0x78, 0x85, 0x85, 0x49, 0xfe },     /* 147 g */
    { 0x00, 0xff, 0x10, 0x20, 0x20, 0x1f },     /* 150 h */
    { 0x00, 0x00, 0x21, 0xbf, 0x01, 0x00 },     /* 151 i */
    { 0x01, 0x02, 0x01, 0x81, 0xfe, 0x00 },     /* 152 j */
    { 0x00, 0xff, 0x08, 0x14, 0x22, 0x21 },     /* 153 k */
    { 0x00, 0x00, 0xfe, 0x01, 0x01, 0x00 },     /* 154 l */
    { 0x00, 0x3f, 0x20, 0x3f, 0x20, 0x3f },     /* 155 m */
    { 0x00, 0x3f, 0x10, 0x20, 0x20, 0x1f },     /* 156 n */
    { 0x00, 0x1e, 0x21, 0x21, 0x21, 0x1e },     /* 157 o */
    { 0x01, 0xff, 0x48, 0x84, 0x84, 0x78 },     /* 160 p */
    { 0x01, 0x78, 0x84, 0x84, 0x48, 0xff },     /* 161 q */
    { 0x00, 0x3f, 0x08, 0x10, 0x20, 0x20 },     /* 162 r */
    { 0x00, 0x12, 0x29, 0x29, 0x29, 0x26 },     /* 163 s */
    { 0x00, 0x20, 0xfe, 0x21, 0x21, 0x00 },     /* 164 t */
    { 0x00, 0x3e, 0x01, 0x01, 0x02, 0x3f },     /* 165 u */
    { 0x00, 0x3c, 0x02, 0x01, 0x02, 0x3c },     /* 166 v */
    { 0x00, 0x3e, 0x01, 0x1e, 0x01, 0x3e },     /* 167 w */
    { 0x00, 0x23, 0x14, 0x08, 0x14, 0x23 },     /* 170 x */
    { 0x01, 0xf8, 0x05, 0x05, 0x09, 0xfe },     /* 171 y */
    { 0x00, 0x23, 0x25, 0x29, 0x31, 0x21 },     /* 172 z */
    { 0x00, 0x18, 0x66, 0x81, 0x81, 0x00 },     /* 173 { */
    { 0x00, 0x00, 0xe7, 0x00, 0x00, 0x00 },     /* 174 | */
    { 0x00, 0x00, 0x81, 0x81, 0x66, 0x18 },     /* 175 } */
    { 0x00, 0x0c, 0x10, 0x08, 0x04, 0x18 },     /* 176 ~ */
    { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff }      /* 177 rubout */
};

/*
 * VS60 character stroke table
 *
 * stroke[] contains "prototype" encodings for all vector strokes (visible and
 * invisible) needed to draw each character at a standard size.  The actual
 * display is of course properly italicized, positioned, scaled, and rotated.
 *
 * Variable-length entries are used; each character stroke sequence is
 * terminated by a 0-valued byte.  Pointers to the appropriate data for all
 * characters are stored into sstroke[] during a one-time initialization.
 *
 * The prototype strokes are for the most part constrained to a 4x6 unit area,
 * except for a few cases that are handled by kludging the coordinates.
 * Coordinates are relative to the left end of the character baseline.
 *
 * A prototype stroke is encoded as 8 bits SVXXXYYY:
 *              S       = 0 if YYY is correct as is
 *                        1 if YYY needs to have 2 subtracted
 *              V       = 0 if stroke is invisible (move)
 *                        1 if stroke is visible (draw)
 *              XXX     = final X coord of stroke (0..4; 7 => -1)
 *              YYY     = final Y coord of stroke (0..6)
 */

static const unsigned char stroke[] = {
    /*
     * While based on the actual VT48 strokes, these have been tweaked
     * (especially the lower-case letters, which had erratic sizes) to
     * improve their appearance and/or reduce the number of strokes.
     * Several of the special symbols (e.g. alpha, delta, iota) could
     * be further improved, but I didn't want to make them look too
     * different from the original.  Note that VS60 screen photos
     * disagree, for several characters, with the (incomplete) chart of
     * strokes given in the VT48 manual.  (There could have been ROM changes.)
     *
     * The simulated character sizes are not exact at all scales, but there
     * is no really good way to fix this without spoiling the appearance.
     * char. scale      VS60 units      simulation units (pixel has size!)
     *     1/2           5   x  7        5 x  7 
     *      1           10   x 14        9 x 13
     *     3/2          15   x 21       13 x 19
     *      2           20   x 28       17 x 25
     */
    0111, 0123, 0006, 0115, 0131, 0140, 0,              /* 000 lambda */
    0042, 0132, 0114, 0103, 0112, 0134, 0144, 0,        /* 001 alpha */
    0011, 0103, 0115, 0135, 0143, 0131, 0111, 0010,
    0146, 0,                                            /* 002 phi */
    0040, 0100, 0133, 0106, 0146, 0,                    /* 003 SIGMA */
    0022, 0111, 0120, 0131, 0113, 0115, 0124, 0,        /* 004 delta */
    0140, 0124, 0100, 0,                                /* 005 DELTA */
    0006, 0126, 0120, 0140, 0,                          /* 006 iota */
    0006, 0115, 0131, 0120, 0111, 0135, 0146, 0,        /* 007 gamma */
    0104, 0116, 0136, 0144, 0140, 0,                    /* 010 intersect */
    0010, 0136, 0044, 0142, 0131, 0111, 0102, 0104, 0,  /* 011 psi */
    0022, 0122, 0003, 0143, 0024, 0124, 0,              /* 012 divide by */
    0024, 0115, 0126, 0135, 0124, 0,                    /* 013 degree */
    0001, 0101, 0025, 0125, 0041, 0141, 0,              /* 014 therefore */
    0111, 0115, 0012, 0121, 0131, 0142, 0045, 0142,
    0151, 0,                                            /* 015 mu */
    0105, 0116, 0126, 0135, 0013, 0173, 0001, 0120,
    0130, 0141, 0,                                      /* 016 pound sterling */
    0,                                                  /* 017 SHIFT IN */
    0003, 0114, 0144, 0034, 0130, 0010, 0114, 0,        /* 020 pi */
    0010, 0116, 0036, 0130, 0,                          /* 021 parallel */
    0110, 0111, 0102, 0104, 0115, 0135, 0144, 0142,
    0131, 0130, 0140, 0,                                /* 022 OMEGA */
    0025, 0134, 0132, 0120, 0110, 0102, 0104, 0146, 0,  /* 023 sigma */
    0010, 0136, 0046, 0116, 0105, 0,                    /* 024 UPSILON */
    0003, 0133, 0045, 0136, 0116, 0105, 0101, 0110,
    0130, 0141, 0,                                      /* 025 epsilon */
    0042, 0102, 0113, 0011, 0102, 0,                    /* 026 left arrow */
    0002, 0142, 0133, 0031, 0142, 0,                    /* 027 right arrow */
    0020, 0124, 0133, 0013, 0124, 0,                    /* 030 up arrow */
    0024, 0120, 0131, 0011, 0120, 0,                    /* 031 down arrow */
    0106, 0146, 0144, 0,                                /* 032 GAMMA */
    0140, 0026, 0120, 0,                                /* 033 perpendicular */
    0001, 0145, 0044, 0104, 0002, 0142, 0,              /* 034 unequal */
    0001, 0112, 0131, 0142, 0044, 0133, 0114, 0103, 0,  /* 035 approx equal */
    0016, 0125, 0135, 0146, 0,                          /* 036 vel */
    0106, 0146, 0140, 0100, 0,                          /* 037 box */
    0,                                                  /* 040 space */
    0020, 0120, 0021, 0125, 0,                          /* 041 ! */
    0004, 0126, 0046, 0124, 0,                          /* 042 " */
    0012, 0116, 0036, 0132, 0043, 0103, 0005, 0145, 0,  /* 043 # */
    0001, 0110, 0130, 0141, 0142, 0133, 0113, 0104,
    0105, 0116, 0136, 0145, 0026, 0120, 0,              /* 044 $ */
    0146, 0116, 0105, 0114, 0125, 0116, 0032, 0141,
    0130, 0121, 0132, 0,                                /* 045 % */
    0040, 0104, 0105, 0116, 0126, 0135, 0134, 0101,
    0110, 0120, 0142, 0,                                /* 046 & */
    0014, 0136, 0,                                      /* 047 ' */
    0030, 0112, 0114, 0136, 0,                          /* 050 ( */
    0010, 0132, 0134, 0116, 0,                          /* 051 ) */
    0002, 0146, 0026, 0122, 0042, 0106, 0,              /* 052 * */
    0021, 0125, 0003, 0143, 0,                          /* 053 + */
    0211, 0120, 0121, 0,                                /* 054 , */
    0003, 0143, 0,                                      /* 055 - */
    0020, 0120, 0,                                      /* 056 . */
    0146, 0,                                            /* 057 / */
    0001, 0145, 0136, 0116, 0105, 0101, 0110, 0130,
    0141, 0145, 0,                                      /* 060 0 */
    0010, 0130, 0020, 0126, 0115, 0,                    /* 061 1 */
    0005, 0116, 0136, 0145, 0144, 0100, 0140, 0,        /* 062 2 */
    0001, 0110, 0130, 0141, 0142, 0133, 0113, 0005,
    0116, 0136, 0145, 0144, 0133, 0,                    /* 063 3 */
    0030, 0136, 0025, 0102, 0142, 0,                    /* 064 4 */
    0001, 0110, 0130, 0141, 0143, 0134, 0114, 0103,
    0106, 0146, 0,                                      /* 065 5 */
    0002, 0113, 0133, 0142, 0141, 0130, 0110, 0101,
    0105, 0116, 0136, 0145, 0,                          /* 066 6 */
    0006, 0146, 0120, 0,                                /* 067 7 */
    0013, 0133, 0142, 0141, 0130, 0110, 0101, 0102,
    0113, 0104, 0105, 0116, 0136, 0145, 0144, 0133, 0,  /* 070 8 */
    0001, 0110, 0130, 0141, 0145, 0136, 0116, 0105,
    0104, 0113, 0133, 0144, 0,                          /* 071 9 */
    0022, 0122, 0024, 0124, 0,                          /* 072 : */
    0010, 0121, 0122, 0024, 0124, 0,                    /* 073 ; */
    0030, 0103, 0136, 0,                                /* 074 < */
    0002, 0142, 0004, 0144, 0,                          /* 075 = */
    0010, 0143, 0116, 0,                                /* 076 > */
    0020, 0120, 0021, 0122, 0144, 0145, 0136, 0116,
    0105, 0104, 0,                                      /* 077 ? */
    0030, 0110, 0101, 0104, 0115, 0145, 0141, 0121,
    0112, 0113, 0124, 0134, 0131, 0,                    /* 100 @ */
    0104, 0116, 0136, 0144, 0140, 0042, 0102, 0,        /* 101 A */
    0106, 0136, 0145, 0144, 0133, 0103, 0033, 0142,
    0141, 0130, 0100, 0,                                /* 102 B */
    0041, 0130, 0110, 0101, 0105, 0116, 0136, 0145, 0,  /* 103 C */
    0106, 0136, 0145, 0141, 0130, 0100, 0,              /* 104 D */
    0003, 0133, 0046, 0106, 0100, 0140, 0,              /* 105 E */
    0106, 0146, 0033, 0103, 0,                          /* 106 F */
    0023, 0143, 0141, 0130, 0110, 0101, 0105, 0116,
    0136, 0145, 0,                                      /* 107 G */
    0106, 0003, 0143, 0046, 0140, 0,                    /* 110 H */
    0010, 0130, 0020, 0126, 0016, 0136, 0,              /* 111 I */
    0001, 0110, 0120, 0131, 0136, 0,                    /* 112 J */
    0106, 0046, 0102, 0024, 0140, 0,                    /* 113 K */
    0006, 0100, 0140, 0,                                /* 114 L */
    0106, 0123, 0146, 0140, 0,                          /* 115 M */
    0106, 0140, 0146, 0,                                /* 116 N */
    0001, 0105, 0116, 0136, 0145, 0141, 0130, 0110,
    0101, 0,                                            /* 117 O */
    0106, 0136, 0145, 0144, 0133, 0103, 0,              /* 120 P */
    0030, 0110, 0101, 0105, 0116, 0136, 0145, 0141,
    0130, 0031, 0140, 0,                                /* 121 Q */
    0106, 0136, 0145, 0144, 0133, 0103, 0033, 0140, 0,  /* 122 R */
    0001, 0110, 0130, 0141, 0142, 0133, 0113, 0104,
    0105, 0116, 0136, 0145, 0,                          /* 123 S */
    0020, 0126, 0006, 0146, 0,                          /* 124 T */
    0006, 0101, 0110, 0130, 0141, 0146, 0,              /* 125 U */
    0006, 0120, 0146, 0,                                /* 126 V */
    0006, 0100, 0123, 0140, 0146, 0,                    /* 127 W */
    0146, 0006, 0140, 0,                                /* 130 X */
    0020, 0123, 0106, 0046, 0123, 0,                    /* 131 Y */
    0006, 0146, 0100, 0140, 0033, 0113, 0,              /* 132 Z */
    0030, 0110, 0116, 0136, 0,                          /* 133 [ */
    0006, 0140, 0,                                      /* 134 \ */
    0010, 0130, 0136, 0116, 0,                          /* 135 ] */
    0003, 0126, 0143, 0,                                /* 136 ^ */
    0140, 0,                                            /* 137 _ */
    0016, 0134, 0,      /* original was backward */     /* 140 ` */
    0032, 0112, 0101, 0110, 0130, 0133, 0124, 0114, 0,  /* 141 a */
    0006, 0100, 0120, 0131, 0133, 0124, 0104, 0,        /* 142 b */
    0033, 0124, 0114, 0103, 0101, 0110, 0120, 0131, 0,  /* 143 c */
    0036, 0130, 0110, 0101, 0103, 0114, 0134, 0,        /* 144 d */
    0002, 0132, 0133, 0124, 0114, 0103, 0101, 0110,
    0120, 0,                                            /* 145 e */
    0010, 0115, 0126, 0136, 0145, 0023, 0103, 0,        /* 146 f */
    0200, 0320, 0331, 0134, 0114, 0103, 0101, 0110,
    0130, 0,                                            /* 147 g */
    0106, 0004, 0124, 0133, 0130, 0,                    /* 150 h */
    0020, 0124, 0025, 0125, 0,                          /* 151 i */
    0201, 0310, 0320, 0331, 0134, 0035, 0135, 0,        /* 152 j */
    0105, 0034, 0101, 0023, 0130, 0,                    /* 153 k */
    0010, 0130, 0020, 0126, 0116, 0,                    /* 154 l */
    0104, 0114, 0122, 0134, 0144, 0140, 0,              /* 155 m */
    0104, 0124, 0133, 0130, 0,                          /* 156 n */
    0010, 0120, 0131, 0133, 0124, 0114, 0103, 0101,
    0110, 0,                                            /* 157 o */
    0200, 0104, 0124, 0133, 0131, 0120, 0100, 0,        /* 160 p */
    0030, 0110, 0101, 0103, 0114, 0134, 0330, 0341, 0,  /* 161 q */
    0104, 0124, 0133, 0,                                /* 162 r */
    0001, 0110, 0120, 0131, 0122, 0112, 0103, 0114,
    0124, 0133, 0,                                      /* 163 s */
    0030, 0121, 0125, 0034, 0114, 0,                    /* 164 t */
    0014, 0111, 0120, 0130, 0141, 0144, 0,              /* 165 u */
    0004, 0120, 0144, 0,                                /* 166 v */
    0004, 0102, 0110, 0122, 0130, 0142, 0144, 0,        /* 167 w */
    0134, 0004, 0130, 0,                                /* 170 x */
    0210, 0120, 0134, 0004, 0120, 0,                    /* 171 y */
    0004, 0134, 0100, 0130, 0,                          /* 172 z */
    0030, 0121, 0122, 0113, 0124, 0125, 0136, 0,        /* 173 { */
    0020, 0122, 0024, 0126, 0,                          /* 174 | */
    0010, 0121, 0122, 0133, 0124, 0125, 0116, 0,        /* 175 } */
    0003, 0114, 0132, 0143, 0,                          /* 176 ~ */
    0140, 0146, 0106, 0100, 0010, 0116, 0026, 0120,
    0030, 0136, 0                                       /* 177 rubout */
    };

/* pointers to start of stroke data for each character */
static const unsigned char *sstroke[128] = { NULL };    /* init. at run time */

/* character generator; supports control chars, POPR on term character (VS60) */

static int      /* returns nonzero iff VS60 char terminate feature triggered */
character(int c)
{
    /* following table maps cs_index to line-feed spacing for VS60 */
    static const unsigned char vs60_csp_h[4] =
                        {PSCALE(12), PSCALE(24), PSCALE(46), PSCALE(62)};
    /* following tables map cs_index to adjustments for sub/superscript */
    /* (cs_index 0 just a guess; others from VS60 Instruction Test Part II) */
    static const unsigned char sus_left[4] =
                                {PSCALE(0), PSCALE(2), PSCALE(4), PSCALE(3)};
    static const unsigned char susr_left[4] =
                                {PSCALE(0), PSCALE(2), PSCALE(4), PSCALE(0)};
    static const unsigned char sub_down[4] =
                                {PSCALE(2), PSCALE(3), PSCALE(6), PSCALE(7)};
    static const unsigned char sup_up[4] =
                                {PSCALE(5), PSCALE(9), PSCALE(18), PSCALE(24)};
    static const unsigned char esus_right[4] =
                                {PSCALE(0), PSCALE(2), PSCALE(0), PSCALE(0)};
    static const unsigned char esub_up[4] =
                                {PSCALE(2), PSCALE(3), PSCALE(6), PSCALE(8)};
    int x, y;
    int32 xbase, ybase, xnext, ynext;

    if (shift_out) {
        if (c >= 040) {
            so_flag = char_irq = 1;     /* will generate a char intr. */
            char_buf = c;
            return 0;                   /* presumably, no POPR on term? */
        }
        if (c == 017) {                 /* SHIFT IN */
            shift_out = 0;
            goto copy;
        }
    } else {    /* !shift_out */

        if (c <= 040) {
            switch (c) {

            case 000:                   /* NULL */
                goto cesc;              /* apparently not copied to char_buf */
            case 010:                   /* BACKSPACE */
                if (char_rotate)
                    ypos -= CSCALE(vt11_csp_w);
                else
                    xpos -= CSCALE(vt11_csp_w);
                break;
            case 012:                   /* LINE FEED */
                if (char_rotate)
                    xpos += (VT11 ? CSCALE(vt11_csp_h) : vs60_csp_h[cs_index]);
                else
                    ypos -= (VT11 ? CSCALE(vt11_csp_h) : vs60_csp_h[cs_index]);
                break;
            case 015:                   /* CARRIAGE RETURN */
                if (char_rotate)
                    ypos = yoff;
                else
                    xpos = xoff;
                break;
            case 016:                   /* SHIFT OUT */
                shift_out = 1;
                break;

            case 021:                   /* SUPERSCRIPT */
                if (VT11)
                    break;
                if (char_rotate) {
                    xpos -= sup_up[cs_index];
                    ypos -= susr_left[cs_index];
                } else {
                    xpos -= sus_left[cs_index];
                    ypos += sup_up[cs_index];
                }
                if (cs_index > 0)
                    char_scale = csi2csf[--cs_index];
                break;
            case 022:                   /* SUBSCRIPT */
                if (VT11)
                    break;
                if (char_rotate) {
                    xpos += sub_down[cs_index];
                    ypos -= susr_left[cs_index];
                } else {
                    xpos -= sus_left[cs_index];
                    ypos -= sub_down[cs_index];
                }
                if (cs_index > 0)
                    char_scale = csi2csf[--cs_index];
                break;
            case 023:                   /* END SUPERSCRIPT */
                if (VT11)
                    break;
                if (cs_index < 3)
                    char_scale = csi2csf[++cs_index];
                if (char_rotate) {
                    xpos += sup_up[cs_index];
                    ypos += esus_right[cs_index];
                } else {
                    xpos += esus_right[cs_index];
                    ypos -= sup_up[cs_index];
                }
                break;
            case 024:                   /* END SUBSCRIPT */
                if (VT11)
                    break;
                if (cs_index < 3)
                    char_scale = csi2csf[++cs_index];
                if (char_rotate) {
                    xpos -= esub_up[cs_index];
                    ypos += esus_right[cs_index];
                } else {
                    xpos += esus_right[cs_index];
                    ypos += esub_up[cs_index];
                }
                break;
            case 040:                   /* SPACE */
                goto space;
            default:                    /* other control codes ignored */
                break;
            }
            goto copy;
        }
    }

    /* VT11/VS60 doesn't draw any part of a character if its *baseline* is
        (partly) offscreen; thus the top of a character might be clipped */
    /* (no allowance for descender, italic, or interchar. spacing) */

    /* virtual CRT coordinates of this and the next character's "origin": */
    xbase = xnext = PNORM(xpos);
    ybase = ynext = PNORM(ypos);
    if (char_rotate)
        ynext += (vt11_csp_w <= 12 ? 10 : 11);
    else
        xnext += (vt11_csp_w <= 12 ? 10 : 11);

    edge_indic = ONSCREEN(xbase, ybase) && !ONSCREEN(xnext, ynext);
    edge_flag = edge_indic ||
                ((!ONSCREEN(xbase, ybase)) &&  ONSCREEN(xnext, ynext));
    /* (scaling cannot make spacing so large that it crosses the
        "working surface" while going from offscreen to offscreen) */
    if (edge_flag) {
        if (edge_intr_ena) {
            edge_irq = 1;
            goto space;
        } else
            edge_flag = 0;
    }

    if (!ONSCREEN(xbase, ybase) || !ONSCREEN(xnext, ynext))
        goto space;

    /* plot a (nominally on-screen) graphic symbol */

    if (VT11) {
        unsigned char col, prvcol;

        /* plot a graphic symbol (unscaled, unrotated) using a dot matrix */

        /* not drawn in a serpentine manner; supports control characters */

        /* draw pattern using 2x2 dot size, with fudges for spacing & italics */
        /* (looks very nice under all conditions at full resolution) */

        if (c >= 0140) {                /* lower-case */
            if (dots[c][0])             /* flag: with descender */
                ybase -= 4;
            x = 1;                      /* skip first column (descender flag) */
        } else                          /* no descender */
            x = 0;

        prvcol = 0;
        col = dots[c][x];               /* starting column bit pattern */
        for (; x < 6; ++x) {
            int xllc = 2*x, yllc = 0;
            unsigned char nxtcol = (x == 5) ? 0 : dots[c][x+1];

            /* no LP hit on first or last column */
            lp_suppress = x == 0 || x == 5;

            for (y = 0; y < 8; ++y) {
                int delay_skew;
                int compress = vt11_csp_w <= 12 && x == 2;
                int dot = col & (1<<y), nxtdot;

                if (dot) {
                    illum2(xbase + xllc, ybase + yllc);
                    if (!compress || (nxtdot = nxtcol & (1<<y)) == 0)
                        illum2(xbase + xllc + 1, ybase + yllc);
                }
                if (italics) {
                    delay_skew = 0;
                    if ((y % 3) != 0
                     && !(delay_skew = ((prvcol & (3<<y))>>y) == 2))
                        ++xllc;         /* shift within selected dots */
                }
                ++yllc;
                if (dot) {
                    illum2(xbase + xllc, ybase + yllc);
                    if (!compress || nxtdot == 0)
                        illum2(xbase + xllc + 1, ybase + yllc);
                }
                if (italics && delay_skew)
                    ++xllc;             /* shift between selected dots */
                ++yllc;
            }
            if (vt11_csp_w <= 12 && x == 2)     /* narrow spacing: */
                --xbase;                /* slight compression */

            prvcol = col;
            col = nxtcol;
        }
        lp_suppress = 0;

    } else {                            /* VS60 */
        const unsigned char *p;         /* -> stroke data */
        unsigned char s;                /* encoded stroke */
        int32 xlast, ylast;             /* "beam follower" within character */
        int32 xp = xpos, yp = ypos;     /* save these (altered by vector2()) */

        /* plot a graphic symbol using vector strokes */

        /* initialize starting stroke pointers upon first use only */
        if (sstroke[0] == NULL) {
            p = stroke; /* -> stroke data */

            for (s = 0; s < 128; ++s) { /* for each ASCII code value s */
                sstroke[s] = p;         /* code's stroke list starts here */
                while (*p++)            /* 0 terminates the data */
                    ;
            }
        }

        stroking = 1;                   /* prevents stroke clipping etc. and
                                           tells vector2() to apply global
                                           character scale factor */
        xlast = ylast = 0;
        for (p = sstroke[c]; (s = *p) != 0; ++p) {
            xnext = (s & 0070) >> 3;
            if (xnext == 7)
                xnext = -1;             /* (kludge needed for pound sterling) */
            ynext = s & 0007;           /* delay stretching for just a moment */
            if (s & 0200)
                ynext -= 2;             /* kludge for stroke below baseline */
            xnext *= 2;
            if (italics)
                xnext += ynext;
            ynext *= 2;                 /* safe to stretch now */

            if (s & 0100) {             /* visible stroke */
                int32 dx = xnext - xlast,       /* (okay if both 0) */
                      dy = ynext - ylast;

                if (char_rotate)
                    vector2(1, -dy, dx);
                else
                    vector2(1, dx, dy);
            } else                      /* invisible stroke, can do faster */
                if (char_rotate) {
                    xpos = xp - CSCALE(ynext);  
                    ypos = yp + CSCALE(xnext);
                } else {
                    xpos = xp + CSCALE(xnext);  
                    ypos = yp + CSCALE(ynext);
                }
            xlast = xnext;
            ylast = ynext;
            skip_start = (s & 0100) && (p[1] & 0100);   /* avoid bright dot */
        }
        /* skip_start was reset to 0 by the last iteration! */
        stroking = 0;
        xpos = xp;                      /* restore for use in spacing (below) */
        ypos = yp;
    }   /* end of graphic character drawing */

  space:
    if (char_rotate)
        ypos += CSCALE(vt11_csp_w);
    else
        xpos += CSCALE(vt11_csp_w);

    /* There may have been multiple LP hits during drawing;
        the last one is the only one that can be reported. */

  copy:
    char_buf = c;

  cesc:
    if (char_escape && c == char_term) {        /* (VS60) */
        pop(1);
        return 1;
    } else
        return 0;
}

/*
 * Perform one display processor "cycle":
 * If display processor is halted or awaiting sync, just performs "background"
 * maintenance tasks and returns 0.
 * Otherwise, draws any pending clipped vector (VS60 only).
 * Otherwise, completes any pending second CHAR or BSVECT (must be a RESUME
 * after interrupt on first CHAR or BSVECT), or fetches one word from the
 * display file and processes it.  May post an interrupt; returns 1 if display
 * processor is still running, or 0 if halted or an interrupt was posted.
 *
 * word_number keeps track of the state of multi-word graphic data parsing;
 * word_number also serves to keep track of half-word for graphic data having
 * two independent entities encoded within one word (CHAR or BSVECT).
 * Note that, for the VT11, there might be control words (e.g. JMPA) embedded
 * within the data!  (We don't know of any application that exploits this.)
 */
int
vt11_cycle(int us, int slowdown)
{
    static vt11word inst;
    static int i;
    static int32 x, y, z, ex, ey, sxo, syo, szo;
    int c;
    int32 ez;
    static uint32 usec = 0;             /* cumulative */
    static uint32 msec = 0;             /* ditto */
    uint32 new_msec;
    INIT
    /* keep running time counter; track state even when processor is idle */

    new_msec = (usec += us) / 1000;

    if (msec / BLINK_COUNT != new_msec / BLINK_COUNT)
        blink_off = !blink_off;

    /* if awaiting sync, look for next frame start */
    if (sync_period && (msec / sync_period != new_msec / sync_period))
        sync_period = 0;                /* start next frame */

    msec = new_msec;

    if ((sync_period || maint1 || !busy) && !maint2)
        goto age_ret;                   /* just age the display */

    /* draw a clipped vector [perhaps after resume from edge interrupt] */

    if (clip_vect) {
        int32 dx = clip_x1 - clip_x0,
              dy = clip_y1 - clip_y0,
              dz = clip_z1 - clip_z0;
        DEBUGF("clipped vector i%d (%ld,%ld,%ld) to (%ld,%ld,%ld)\r\n", clip_i,
               (long)clip_x0, (long)clip_y0, (long)clip_z0,
               (long)clip_x1, (long)clip_y1, (long)clip_z1);
        if (VS60                        /* XXX  assuming VT11 doesn't display */
         && (dx != 0 || dy != 0 || dz != 0)     /* hardware skips null vects */
         && clip_i && int0_scope) {     /* show it */
            if (menu)
                lineTwoStep(clip_x0 + MENU_OFFSET, clip_y0, clip_z0,
                            clip_x1 + MENU_OFFSET, clip_y1, clip_z1);
            else
                lineTwoStep(clip_x0, clip_y0, clip_z0,
                            clip_x1, clip_y1, clip_z1);
        }
        /*
         * In case of LP hit, recompute coords using "tangent register",
         * because:
         *  (1) distinct virtual CRT points can be mapped into the same pixel
         *  (2) raster computation might not match that of the actual VT48
         */
        if (lp0_hit) {
            long tangent;
            int32 adx = ABS(dx), ady = ABS(dy);
            if (adx >= ady) {
                tangent = 010000L * dy / dx;    /* signed */
                lp_ypos = clip_y0 + tangent * (lp_xpos - clip_x0) / 010000L;
                tangent = 010000L * dz / dx;
                lp_zpos = clip_z0 + tangent * (lp_xpos - clip_x0) / 010000L;
            } else {
                tangent = 010000L * dx / dy;    /* signed */
                lp_xpos = clip_x0 + tangent * (lp_ypos - clip_y0) / 010000L;
                tangent = 010000L * dz / dy;
                lp_zpos = clip_z0 + tangent * (lp_ypos - clip_y0) / 010000L;
            }
            DEBUGF("adjusted LP coords (0%o,0%o,0%o)\r\n",
                   lp_xpos, lp_ypos, lp_zpos);
            /* xpos,ypos,zpos still pertain to the original endpoint
               (assuming that Maintenance Switch 3 isn't set) */
        }
        if (VS60) {                     /* XXX  assuming just 1 intr for VT11 */
            edge_xpos = clip_x1;
            edge_ypos = clip_y1;
            edge_zpos = clip_z1;
            edge_indic = (clip_vect & 2) != 0;  /* indicate clipped going out */
            edge_flag = edge_intr_ena;
            if (edge_flag) {
                edge_irq = 1;
                vt_lpen_intr();         /* post graphic interrupt to host */
            }
        }
        clip_vect = 0;                  /* this finishes the condition */
        goto check;                     /* possibly post more interrupts; age */
    }

    /* fetch next word from display file (if needed) and process it */

    if (word_number != 1 || (graphic_mode != CHAR && graphic_mode != BSVECT)) {
        time_out = vt_fetch((uint32)((DPC+reloc)&0777777), &inst);
        DPC += 2;
        if (time_out)
            goto bus_timeout;
        DEBUGF("0%06o: 0%06o\r\n",
               (unsigned)(DPC - 2 + reloc) & 0777777, (unsigned)inst);
        if (finish_jmpa)
            goto jmpa;
        if (finish_jsra)
            goto jsra;
    }
    /* else have processed only half the CHAR or BSVECT data word so far */

  fetched:

    if (TESTBIT(inst,15)) {             /* control */
        unsigned op;
        mode_field = GETFIELD(inst,14,11);      /* save bits 14-11 for diags. */
        word_number = -1;               /* flags "control mode"; ersatz 0 */
        switch (mode_field) {

        case 7:                         /* Set Graphic Mode 0111 */
        case 011:                       /* Set Graphic Mode 1001 */
            if (VT11)
                goto bad_ins;
            /*FALLTHRU*/
        case 010:                       /* Set Graphic Mode 1000 */
            if (VT11) {
                DEBUGF("SGM 1000 IGNORED\r\n");
                break;
            }
            /*FALLTHRU*/
        case 0:                         /* Set Graphic Mode 0000 */
        case 1:                         /* Set Graphic Mode 0001 */
        case 2:                         /* Set Graphic Mode 0010 */
        case 3:                         /* Set Graphic Mode 0011 */
        case 4:                         /* Set Graphic Mode 0100 */
        case 5:                         /* Set Graphic Mode 0101 */
        case 6:                         /* Set Graphic Mode 0110 */
            DEBUGF("Set Graphic Mode %u", (unsigned)mode_field);
            graphic_mode = (enum gmode)mode_field;
            offset = 0;
            shift_out = 0;              /* seems to be right */
            if (TESTBIT(inst,10)) {
                intensity = GETFIELD(inst,9,7);
                DEBUGF(" intensity=%d", (int)intensity);
            }
            if (TESTBIT(inst,6)) {
                lp0_intr_ena = TESTBIT(inst,5);
                DEBUGF(" lp0_intr_ena=%d", (int)lp0_intr_ena);
            }
            if (TESTBIT(inst,4)) {
                blink_ena = TESTBIT(inst,3);
                DEBUGF(" blink=%d", (int)blink_ena);
            }
            if (TESTBIT(inst,2)) {
                line_type = (enum linetype)GETFIELD(inst,1,0);
                DEBUGF(" line_type=%d", (int)line_type);
            }
            DEBUGF("\r\n");
            break;

        case 012:                       /* 1010: Load Name Register */
            if (VT11)
                goto bad_ins;
            name = GETFIELD(inst,10,0);
            DEBUGF("Load Name Register name=0%o\r\n", name);
            {   static unsigned nmask[4] = { 0, 03777, 03770, 03600 };

                if (search != 0 && ((name^assoc_name) & nmask[search]) == 0)
                    name_irq = 1;       /* will cause name-match interrupt */
            }
            break;

        case 013:                       /* 1011: Load Status C */
            if (VT11)
                goto bad_ins;
            DEBUGF("Load Status C");
            if (TESTBIT(inst,9)) {
                char_rotate = TESTBIT(inst,8);
                DEBUGF(" char_rotate=d", (int)char_rotate);
            }
            if (TESTBIT(inst,7)) {
                cs_index = GETFIELD(inst,6,5);  /*  0, 1, 2, 3 */
                char_scale = csi2csf[cs_index]; /* for faster CSCALE macro */
                DEBUGF(" cs_index=%d(x%d/4)", (int)cs_index, (int)char_scale);
            }
            if (TESTBIT(inst,4)) {
                vector_scale = GETFIELD(inst,3,0);
                DEBUGF(" vector_scale=%d/4", (int)vector_scale);
            }
            DEBUGF("\r\n");
            break;

        case 014:                       /* 1100__ */
            if (VT11)                   /* other bits are "spare" */
                op = 0;                 /* always Display Jump Absolute */
            else
                op = GETFIELD(inst,10,9);
            switch (op) {

            case 0:                     /* 110000: Display Jump Absolute */
                finish_jmpa = 1;
                break;
  jmpa:
                finish_jmpa = 0;
                DPC = inst & ~1;
                DEBUGF("Display Jump Absolute 0%06o\r\n", (unsigned)inst);
                break;

            case 1:                     /* 110001: Display Jump Relative */
                ez = GETFIELD(inst,7,0);/* relative address (words) */
                ez *= 2;                /* convert to bytes */
                /* have to be careful; DPC is unsigned */
                if (TESTBIT(inst,8)) {
#if 0                   /* manual seems to say this, but it's wrong: */
                        DPC -= ez;
                        DEBUGF("Display Jump Relative -0%o\r\n",
                               (unsigned)ez);
#else                   /* sign extend, twos complement add, 16-bit wrapping */
                        DPC = (DPC + (~0777 | ez)) & 0177777;
                        DEBUGF("Display Jump Relative -0%o\r\n",
                               ~((~0777 | ez) - 1));
#endif
                } else {
                        DPC += (vt11word)ez;
                        DEBUGF("Display Jump Relative +0%o\r\n",
                               (unsigned)ez);
                }
                /* DPC was already incremented by 2 */
                break;

            case 2:            /* 110010: Display Jump to Subroutine Absolute */
                finish_jsra = 1;
                jsr = 1;                /* diagnostic test needs this here */
                /* but the documentation says JSR bit set only for JSR REL! */
                goto check;             /* (break would set jsr = 0) */
  jsra:
                finish_jsra = 0;
                push();                 /* save return address and parameters */
                DPC = inst & ~1;
                DEBUGF("Display Jump to Subroutine Absolute 0%06o\r\n",
                       (unsigned)inst);
                goto check;             /* (break would set jsr = 0) */

            case 3:            /* 110011: Display Jump to Subroutine Relative */
                ez = GETFIELD(inst,7,0);/* relative address (words) */
                ez *= 2;                /* convert to bytes */
                push();                 /* save return address and parameters */
                /* have to be careful; DPC is unsigned */
                if (TESTBIT(inst,8)) {
#if 0                   /* manual seems to say this, but it's wrong: */
                        DPC -= (vt11word)ez;
                        DEBUGF("Display Jump to Subroutine Relative -0%o\r\n",
                               (unsigned)ez);
#else                   /* sign extend, twos complement add, 16-bit wrapping */
                        DPC = (DPC + (~0777 | ez)) & 0177777;
                        DEBUGF("Display Jump to Subroutine Relative -0%o\r\n",
                               ~((~0777 | ez) - 1));
#endif
                } else {
                        DPC += (vt11word)ez;
                        DEBUGF("Display Jump to Subroutine Relative +0%o\r\n",
                               (unsigned)ez);
                }
                /* DPC was already incremented by 2 */
                break;                  /* jsr = 0 ?? */
            }
            break;

        case 015:                       /* 1101__ */
            if (VT11)
                DEBUGF("Display NOP\r\n");
            else {
                op = GETFIELD(inst,10,9);
                switch (op) {

                case 0:                 /* 110100: Load Scope Selection */
                                        /* also used as Display NOP */
                    DEBUGF("Load Scope Selection");
                    c = TESTBIT(inst,8);
                    DEBUGF(" console=%d", c);
                    if (TESTBIT(inst,7)) {
                        ez = TESTBIT(inst,6);
                        DEBUGF(" blank=%d", (int)!ez);
                        if (c)
                            int1_scope = (unsigned char)(ez & 0xFF);
                        else
                            int0_scope = (unsigned char)(ez & 0xFF);
                    }
                    if (TESTBIT(inst,5)) {
                        ez = TESTBIT(inst,4);
                        DEBUGF(" lp_intr_ena=%d", (int)ez);
                        if (c)
                            lp1_intr_ena = (unsigned char)(ez & 0xFF);
                        else
                            lp0_intr_ena = (unsigned char)(ez & 0xFF);
                    }
                    if (TESTBIT(inst,3)) {
                        ez = TESTBIT(inst,2);
                        DEBUGF(" lp_sw_intr_ena=%d", (int)ez);
                        if (c)
                            lp1_sw_intr_ena = (unsigned char)(ez & 0xFF);
                        else
                            lp0_sw_intr_ena = (unsigned char)(ez & 0xFF);
                    }
                    DEBUGF("\r\n");
                    break;

                case 1:                 /* 110101: Display POP Not Restore */
                    DEBUGF("Display POP Not Restore\r\n");
                    pop(0);             /* sets new DPC as side effect */
                    break;

                case 2:                 /* 110110: Display POP Restore */
                    DEBUGF("Display POP Restore\r\n");
                    pop(1);             /* sets new DPC as side effect */
                    break;

                default:                /* 110111: undocumented -- ignored? */
                    DEBUGF("Display NOP?\r\n");
                }
            }
            break;

        case 016:                       /* 1110: Load Status A */
            DEBUGF("Load Status A");
            internal_stop = TESTBIT(inst,10);   /* 11101 Display Stop */
            if (internal_stop) {
                stopped = 1;            /* (synchronous with display cycle) */
                DEBUGF(" stop");
            }
            if (TESTBIT(inst,9)) {
                stop_intr_ena  = TESTBIT(inst,8);
                DEBUGF(" stop_intr_ena=%d", (int)stop_intr_ena);
            }
            if (TESTBIT(inst,7)) {
                lp_intensify = !TESTBIT(inst,6);
                DEBUGF(" lp_intensify=%d", (int)lp_intensify);
            }
            if (TESTBIT(inst,5)) {
                italics = TESTBIT(inst,4);
                DEBUGF(" italics=%d", (int)italics);
            }
            refresh_rate = GETFIELD(inst,VS60?3:2,2);
            DEBUGF(" refresh=%d", refresh_rate);
            if (sync_period != refresh_rate)
                DEBUGF("old sync_period=%d, new refresh=%d", sync_period, refresh_rate);
            switch (refresh_rate) {
            case 0:                     /* continuous */
                sync_period = 0;
                break;
            case 1:                     /* VT11: 60 Hz; VS60: 30 Hz */
                sync_period = VT11 ? 17 : 33;
                break;
            case 2:                     /* VS60: 40 Hz */
                sync_period = 25;
                break;
            default:                    /* (case 3)  VS60: external sync */
                sync_period = 17;       /* fake a 60 Hz source */
                break;
            }
            if (internal_stop) {
                sync_period = 0;        /* overridden */
            }
            if (VS60 && TESTBIT(inst,1)) {
                menu = TESTBIT(inst,0);
                DEBUGF(" menu=%d", (int)menu);
            }
            DEBUGF("\r\n");
            break;

        case 017:                       /* 1111_ */
            if (VS60 && TESTBIT(inst,10)) {     /* 11111: Load Status BB */
                DEBUGF("Load Status BB");
                if (TESTBIT(inst,7)) {
                    depth_cue_proc = TESTBIT(inst,6);
                    DEBUGF(" depth_cue_proc=%d", (int)depth_cue_proc);
                }
                if (TESTBIT(inst,5)) {
                    edge_intr_ena = TESTBIT(inst,4);
                    DEBUGF(" edge_intr_ena=%d", (int)edge_intr_ena);
                }
                if (TESTBIT(inst,3)) {
                    file_z_data = TESTBIT(inst,2);
                    DEBUGF(" file_z_data=%d", (int)file_z_data);
                }
                if (TESTBIT(inst,1)) {
                    char_escape = TESTBIT(inst,0);
                    DEBUGF(" char_escape=%d", (int)char_escape);
                }
            } else {                            /* 11110: Load Status B */
                DEBUGF("Load Status B");
                if (VS60 && TESTBIT(inst,9)) {
                    color = (enum scolor)GETFIELD(inst,8,7);
                    DEBUGF(" color=%d", (int)color);
                }
                if (TESTBIT(inst,6)) {
                    graphplot_step = GETFIELD(inst,5,0);
                    DEBUGF(" graphplot_step=%d", (int)graphplot_step);
                }
            }
            DEBUGF("\r\n");
            break;

        default:
  bad_ins:  DEBUGF("SPARE COMMAND 0%o\r\n", mode_field);
            /* "display processor hangs" */
            DPC -= 2;                   /* hang around scene of crime */
            break;

        } /* end of control instruction opcode switch */
        jsr = 0;

    } else {                            /* graphic data */

#if 0   /* XXX ? */
        lp0_hit = 0;                    /* XXX  maybe not for OFFSET? */
#endif
        if (word_number < 0)            /* (after reset or control instr.) */
                word_number = 0;
        if (word_number == 0)
            offset = 0;

#define MORE_DATA       { ++word_number; goto check; }

        switch (mode_field = graphic_mode) {    /* save for MPR read */

        case CHAR:
            if (word_number > 1)
                word_number = 0;
            if (word_number == 0) {
                c = GETFIELD(inst,6,0);
                DEBUGF("char1 %d (", c);
                    DEBUGF(040 <= c && c < 0177 ? "'%c'" : "0%o", c);
                        DEBUGF(")\r\n");
                if (character(c))       /* POPR was done; end chars */
                    break;
                MORE_DATA               /* post any intrs now */
            }
            c = GETFIELD(inst,15,8);
            DEBUGF("char2 %d (", c);
                DEBUGF(040 <= c && c < 0177 ? "'%c'" : "0%o", c);
                    DEBUGF(")\r\n");
            (void)character(c);
            break;

        case SVECTOR:
            if (word_number > 1 || (!file_z_data && word_number > 0))
                word_number = 0;
            if (word_number == 0) {
                i = TESTBIT(inst,14);   /* inten_ena: beam on */
                x = GETFIELD(inst,12,7);/* delta_x */
                if (TESTBIT(inst,13))
                    x = -x;
                y = GETFIELD(inst,5,0); /* delta_y */
                if (TESTBIT(inst,6))
                    y = -y;
                if (file_z_data)
                    MORE_DATA
            }
            if (file_z_data) {          /* (VS60) */
                z = GETFIELD(inst,9,2); /* delta_z */
                if (TESTBIT(inst,13))
                        z = -z;
                DEBUGF("short vector i%d (%d,%d,%d)\r\n",
                       i, (int)x, (int)y, (int)z);
                vector3(i, x, y, z);
            } else {
                DEBUGF("short vector i%d (%d,%d)\r\n", i, (int)x, (int)y);
                vector2(i, x, y);
            }
            break;

        case LVECTOR:
            if (word_number > 2 || (!file_z_data && word_number > 1))
                word_number = 0;
            if (word_number == 0) {
                ex = VS60 && TESTBIT(inst,12);
                i = TESTBIT(inst,14);
                x = GETFIELD(inst,9,0); /* delta_x */
                if (TESTBIT(inst,13))
                    x = -x;
                MORE_DATA
            }
            if (word_number == 1) {
                y = GETFIELD(inst,9,0); /* delta_y */
                if (TESTBIT(inst,13))
                    y = -y;
                if (file_z_data)
                    MORE_DATA
            }
            if (file_z_data) {          /* (VS60) */
                if (ex)
                    goto norot;
                z = GETFIELD(inst,9,2); /* delta_z */
                if (TESTBIT(inst,13))
                        z = -z;
                DEBUGF("long vector i%d (%d,%d,%d)\r\n",
                       i, (int)x, (int)y, (int)z);
                vector3(i, x, y, z);
            } else {
                if (ex)
  norot:            /* undocumented and probably nonfunctional */
                    DEBUGF("ROTATE NOT SUPPORTED\r\n");
                else {
                    DEBUGF("long vector i%d (%d,%d)\r\n", i, (int)x, (int)y);
                    vector2(i, x, y);
                }
            }
            break;

        case POINT:                     /* (or OFFSET, if VS60) */
            /* [VT48 manual incorrectly says point data doesn't use sign bit] */
            if (word_number > 2 || (!file_z_data && word_number > 1))
                word_number = 0;
            if (word_number == 0) {
                ex = GETFIELD(inst,(VS60?11:9),0);
                offset = VS60 && TESTBIT(inst,12);      /* offset flag */
                if (!offset)
                    i = TESTBIT(inst,14);       /* for point only */
                if (VS60) {
                    sxo = TESTBIT(inst,13);     /* sign bit */
                    if (sxo)
                        ex = -ex;
                }
                /* XXX  if VT11, set xpos/xoff now?? */
                MORE_DATA
            }
            if (word_number == 1) {
                ey = GETFIELD(inst,(VS60?11:9),0);
                if (VS60) {
                    syo = TESTBIT(inst,13);     /* sign bit */
                    if (syo)
                        ey = -ey;
                }
                if (file_z_data)
                    MORE_DATA
            }
            if (file_z_data) {          /* (VS60) */
                ez = GETFIELD(inst,11,2);
                szo = TESTBIT(inst,13);         /* sign bit */
                if (szo)
                    ez = -ez;
                if (offset) {           /* OFFSET rather than POINT */
                    DEBUGF("offset (%d,%d,%d)\r\n", (int)ex,(int)ey,(int)ez);
                    xoff = PSCALE(ex);
                    yoff = PSCALE(ey);
                    zoff = PSCALE(ez * 4);      /* XXX  include bits 1:0 ? */
                    s_xoff = (unsigned char)(sxo & 0xFF);
                    s_yoff = (unsigned char)(syo & 0xFF);
                    s_zoff = (unsigned char)(szo & 0xFF);
                } else {
                    DEBUGF("point i%d (%d,%d,%d)\r\n", i,
                           (int)ex, (int)ey, (int)ez);
                    point3(i, VSCALE(ex) + xoff, VSCALE(ey) + yoff,
                                VSCALE(ez * 4) + zoff, VS60);
                }
            } else {
                if (offset) {           /* (VS60) OFFSET rather than POINT */
                    DEBUGF("offset (%d,%d)\r\n", (int)ex, (int)ey);
                    xoff = PSCALE(ex);
                    yoff = PSCALE(ey);
                    s_xoff = (unsigned char)(sxo & 0xFF);
                    s_yoff = (unsigned char)(syo & 0xFF);
                } else {
                    DEBUGF("point i%d (%d,%d)\r\n", i, (int)ex, (int)ey);
                    point2(i, VSCALE(ex) + xoff, VSCALE(ey) + yoff, VS60);
                }
            }
            break;

        case GRAPHX:                    /* (or BLVECT if VS60) */
            word_number = 0;
            i = TESTBIT(inst,14);
            if (VS60 && TESTBIT(inst,10))
                goto blv;               /* (VS60) BLVECT rather than GRAPHX */
            else {
                ex = GETFIELD(inst,9,0);
                DEBUGF("graphplot x (%d) i%d\r\n", (int)ex, i);
                ey = ypos + VSCALE(graphplot_step);
                /* VT48 ES says first datum doesn't increment Y; that's wrong */
                /* diagnostic DZVSD shows that "i" bit is ignored! */
                point2(1, VSCALE(ex) + xoff, ey, VS60);
            }
            break;

        case GRAPHY:                    /* (or BLVECT if VS60) */
            word_number = 0;
            i = TESTBIT(inst,14);
            if (VS60 && TESTBIT(inst,10)) {
  blv:                                  /* (VS60) BLVECT rather than GRAPHY */
                x = GETFIELD(inst,13,11);       /* direction */
                y = GETFIELD(inst,9,0);         /* length */
                DEBUGF("basic long vector i%d d%d l%d\r\n",
                       i, (int)x, (int)y);
                basic_vector(i, (int)x, (int)y);
            } else {
                ey = GETFIELD(inst,9,0);
                DEBUGF("graphplot y (%d) i%d\r\n", (int)ey, i);
                ex = xpos + VSCALE(graphplot_step);
                /* VT48 ES says first datum doesn't increment X; that's wrong */
                /* diagnostic DZVSD shows that "i" bit is ignored! */
                point2(1, ex, VSCALE(ey) + yoff, VS60);
            }
            break;

        case RELPOINT:
            if (word_number > 1 || (!file_z_data && word_number > 0))
                word_number = 0;
            if (word_number == 0) {
                i = TESTBIT(inst,14);
                ex = GETFIELD(inst,12,7);
                if (TESTBIT(inst,13))
                    ex = -ex;
                ey = GETFIELD(inst,5,0);
                if (TESTBIT(inst,6))
                    ey = -ey;
                if (file_z_data)
                    MORE_DATA
            }
            if (file_z_data) {          /* (VS60) */
                ez = GETFIELD(inst,9,2);
                if (TESTBIT(inst,13))
                    ez = -ez;
                DEBUGF("relative point i%d (%d,%d,%d)\r\n",
                       i, (int)ex, (int)ey, (int)ez);
                point3(i, xpos + VSCALE(ex), ypos + VSCALE(ey),
                        zpos + VSCALE(ez * 4), 1);
            } else {
                DEBUGF("relative point i%d (%d,%d)\r\n", i, (int)ex, (int)ey);
                point2(i, xpos + VSCALE(ex), ypos + VSCALE(ey), 1);
            }
            break;

        /* the remaining graphic data types are supported by the VS60 only */

        case BSVECT:                    /* (VS60) */
            if (word_number > 1)
                word_number = 0;
            if (word_number == 0) {
                i = TESTBIT(inst,14);
                x = GETFIELD(inst,6,4);         /* direction 0 */
                y = GETFIELD(inst,3,0);         /* length 0 */
                ex = GETFIELD(inst,13,11);      /* direction 1 */
                ey = GETFIELD(inst,10,7);       /* length 1 */
                DEBUGF("basic short vector1 i%d d%d l%d\r\n",
                       i, (int)x, (int)y);
                basic_vector(i, (int)x, (int)y);
                if (lphit_irq || edge_irq)      /* MORE_DATA skips this */
                    vt_lpen_intr();     /* post graphic interrupt to host */
                MORE_DATA
            }
            DEBUGF("basic short vector2 i%d d%d l%d\r\n", i, (int)ex,(int)ey);
            basic_vector(i, (int)ex, (int)ey);
            break;

        case ABSVECTOR:                 /* (VS60) */
            /* Note: real VS60 can't handle a delta of more than +-4095 */
            if (word_number > 2 || (!file_z_data && word_number > 1))
                word_number = 0;
            if (word_number == 0) {
                i = TESTBIT(inst,14);
                x = GETFIELD(inst,11,0);
                if (TESTBIT(inst,13))
                        x = -x;
                MORE_DATA
            }
            if (word_number == 1) {
                y = GETFIELD(inst,11,0);
                if (TESTBIT(inst,13))
                        y = -y;
                if (file_z_data)
                    MORE_DATA
            }
            if (file_z_data) {
                z = GETFIELD(inst,11,2);
                if (TESTBIT(inst,13))
                        z = -z;
                DEBUGF("absolute vector i%d (%d,%d,%d)\r\n",
                       i, (int)x, (int)y, (int)z);
                ex = VSCALE(x) + xoff;
                ey = VSCALE(y) + yoff;
                ez = VSCALE(z * 4) + zoff;
                vector3(i, PNORM(ex - xpos), PNORM(ey - ypos),
                           PNORM(ez - zpos) / 4);       /* approx. */
                zpos = ez;              /* more precise, if PSCALEF > 1 */
            } else {
                DEBUGF("absolute vector i%d (%d,%d)\r\n", i, (int)x, (int)y);
                ex = VSCALE(x) + xoff;
                ey = VSCALE(y) + yoff;
                vector2(i, PNORM(ex - xpos), PNORM(ey - ypos)); /* approx. */
            }
            xpos = ex;                  /* more precise, if PSCALEF > 1 */
            ypos = ey;
            break;

        case CIRCLE:                    /* (VS60) */
            if (word_number > 5 || (!file_z_data && word_number > 3))
                word_number = 0;
            if (word_number == 0) {
                i = TESTBIT(inst,14);
                x = GETFIELD(inst,9,0); /* delta cx */
                if (TESTBIT(inst,13))
                    x = -x;
                MORE_DATA
            }
            if (word_number == 1) {
                y = GETFIELD(inst,9,0); /* delta cy */
                if (TESTBIT(inst,13))
                    y = -y;
                MORE_DATA
            }
            if (word_number == 2) {
                if (file_z_data) {
                    z = GETFIELD(inst,11,2);    /* delta cz */
                    if (TESTBIT(inst,13))
                        z = -z;
                    MORE_DATA
                }
            }
            if (word_number == 2 + file_z_data) {
                ex = GETFIELD(inst,9,0);        /* delta ex */
                if (TESTBIT(inst,13))
                    ex = -ex;
                MORE_DATA
            }
            if (word_number == 3 + file_z_data) {
                ey = GETFIELD(inst,9,0);        /* delta ey */
                if (TESTBIT(inst,13))
                    ey = -ey;
                if (file_z_data)
                    MORE_DATA
            }
            if (file_z_data) {
                ez = GETFIELD(inst,11,2);       /* delta ez */
                if (TESTBIT(inst,13))
                    ez = -ez;
                DEBUGF("circle/arc i%d C(%d,%d,%d) E(%d,%d,%d)\r\n",
                       i, (int)x, (int)y, (int)z, (int)ex, (int)ey, (int)ez);
                conic3(i, x, y, z, ex, ey, ez); /* approx. */
            } else {
                DEBUGF("circle/arc i%d C(%d,%d) E(%d,%d)\r\n",
                       i, (int)x, (int)y, (int)ex, (int)ey);
                conic2(i, x, y, ex, ey);
            }
            break;

        default:                        /* "can't happen" */
            DPC -= 2;                   /* hang around scene of crime */
            break;

        } /* end of graphic_mode switch */
        ++word_number;

        /* LP hit & edge interrupts triggered only while in data mode */
        if (lphit_irq || edge_irq)
            vt_lpen_intr();             /* post graphic interrupt to host */

    } /* end of instruction decoding and execution */
    goto check;

  bus_timeout:
    DEBUGF("TIMEOUT\r\n");
    /* fall through to check (time_out has already been set) */

  check:

    /* post an interrupt if conditions are right;
       because this simulation has no pipeline, only one is active at a time */

    if (lp0_sw_state != display_lp_sw) {        /* tip-switch state change */
        lp0_sw_state = display_lp_sw;   /* track switch state */
        lp0_up = !(lp0_down = lp0_sw_state);    /* set transition flags */
        if (lp0_sw_intr_ena)
            lpsw_irq = 1;
    }

    if (lpsw_irq)       /* (LP hit or edge interrupt already triggered above) */
        vt_lpen_intr();                 /* post graphic interrupt to host */
    else if (internal_stop && stop_intr_ena)    /* ext_stop does immediately */
        vt_stop_intr();                 /* post stop interrupt to host */
    else if (char_irq || stack_over || stack_under || time_out)
        vt_char_intr();                 /* post character interrupt to host */
    else if (name_irq)
        vt_name_intr();                 /* post name-match interrupt to host */
#if 1                                   /* risky? */
    else                                /* handle any pending 2nd CHAR/BSVECT */
        if (word_number == 1 && (graphic_mode==CHAR || graphic_mode==BSVECT))
            goto fetched;
#endif

    /* fall through to age_ret */

  age_ret:
    display_age(us, slowdown);
    return !maint1 && !maint2 && busy;
} /* vt11_cycle */
