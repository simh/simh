/* ka10_dpy.c: 340 display subsystem simulator w/ PDP-6 344 interface!

   Copyright (c) 2018, Philip L. Budne

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
   PHILIP BUDNE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Philip Budne shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell.

*/

/*
 * NOTE!!! Currently only supports 340 display using the type 344 interface
 * for PDP-6 described in
 * http://www.bitsavers.org/pdf/dec/graphics/H-340_Type_340_Precision_Incremental_CRT_System_Nov64.pdf
 *
 * The MIT file .INFO.;340 INFO says:
 *    ;CONI BITS OF THE 340 ARE:
 *            ;2.9-2.7        MODE
 *            ;2.4            VECT CONT LP(????)
 *            ;2.3            VERTICAL EDGE HIT
 *            ;2.2            LIGHT PEN HIT
 *            ;2.1            HORIZONTAL EDGE HIT
 *            ;1.9            STOP
 *            ;1.8            DONE (CAUSES DATA INTERUPT)
 *            ;1.6-1.4        SPECIAL PIA
 *            ;1.3-1.1        DATA PIA
 *    ;340 CONO BITS ARE:
 *            ;2.3            CLEAR NO INK MODE
 *            ;2.2            SET NO INK MODE (IN NO INK MODE, NO INTENSIFICATION CAN \
 *     OCCUR)
 *            ;2.1            CLEAR HALF WORD MODE
 *            ;1.9            SET HALF WORD MODE
 *            ;1.8            RESUME DISPLAY (TO CONTINUE AFTER A SPECIAL INTERUPT)
 *            ;1.7            INTIALIZE
 *            ;1.6-1.4        SPECIAL PIA
 *            ;1.3-1.1        DATA PIA
 * ITS uses the "resume display" bit, so it has been implemented here.
 *
 * 340C was used in the PDP-10 VB10C display system
 * http://bitsavers.informatik.uni-stuttgart.de/pdf/dec/pdp10/periph/VB10C_Interactive_Graphics_Terminal_Jul70.pdf
 *      "The basic hardware system consists of a 340/C display connected
 *      directly to PDP-1O memory through a special memory channel.  Several
 *      important features included in the VB-10/C display are memory
 *      protection and relocation, slave mode operation, raster mode, and
 *      subroutining."
 *
 * reading 6.03 VBCSER
 * http://pdp-10.trailing-edge.com/dec-10-omona-u-mc9/01/vbcser.mac.html
 * There appear to be differences in the DIS (130) CONI/O bits:
 * CONI
 *       47400 are "DISPLAY INTERRUPT" bits
 *       45000 are "ILLEGAL ADDRESS OR EDGE FLAG" (same VE/HE??)
 *       40000 is "ILLEGAL ADDR"
 *        2000 is LP HIT (same)
 *         400 is STOP (same
 * CONO
 *       20000 "lock display out of memory"
 *         100 init? (same)
 *          40 "clear flags"
 *          20 "resume" (stored @DISIN4)
 *           7 PI channel?
 *   DISCON = CHAN + 140 (continue?)
 * *NO* DATAO or BLKO to device 130!
 *
 * It appears that the reloc/protect mechanism is on I/O device 134.
 * (referred to by number, not symbol!)
 * DATAO sets reloc/protect, start addr
 *   possibly:
 *      high order 8 protection bits are left justified in left half
 *      high order 8 relocation bits are left justified in right half
 *
 * Other PDP-6/10 display interfaces:
 *
 * http://bitsavers.trailing-edge.com/pdf/dec/graphics/348_Manual_1964.pdf
 * Type 348 interface to Type 30A or 30E displays.
 * "To the display, the interface looks like a PDP-1 computer"
 *
 * Also VP10/VR30 (phone book p. 487):
 *      control word format:
 *      INTENSITY(*),,0 ("4 dimmest, thru 13 brightest" default is 10)
 *      YPOS,,XPOS      (10 bit positions)
 *
 * (*)6.03 DISSER.MAC says:
 * ONLY FOR VP10 and TYPE 30.
 * N IS 3 BITS WIDE FOR 30, 2 BITS WIDE FOR VP10.
 *
 * 348 manual p.10 says: "The three flip-flops are treated as a two bit
 * signed binary number.  Negative numbers are in two's complement form.
 * The most negative number (100) will produce the least intensity.
 * The largest positive number (011) results in the greatest intensity.
 */

#include "kx10_defs.h"
#include "sim_video.h"

#ifndef NUM_DEVS_DPY
#define NUM_DEVS_DPY 0
#endif

#if (NUM_DEVS_DPY > 0)
#include "display/type340.h"
#include "display/display.h"

#define DPY_DEVNUM       0130

#define RRZ(W) ((W) & RMASK)
#define XWD(L,R) ((((uint64)(L))<<18)|((uint64)(R)))

#if PDP6 | KA | KI
extern uint64 SW;        /* switch register */
#endif

/*
 * number of (real?) microseconds between svc calls
 * used to age display, poll for WS events
 * and delay "data" interrupt
 * (VB10C could steal cycles)
 */
#define DPY_CYCLE_US    50

/*
 * number of DPY_CYCLES to delay int
 * too small and host CPU doesn't run enough!
 */
#define INT_COUNT       (100/DPY_CYCLE_US)

#define STAT_REG        u3
#define INT_COUNTDOWN   u4
#define XPOS            us9             /* from LP hit */
#define YPOS            us10            /* from LP hit */

/* STAT_REG */
#define STAT_VALID      01000000        /* internal: invisible to PDP-10 */

/* CONI/CONO */
/* http://www.bitsavers.org/pdf/dec/graphics/H-340_Type_340_Precision_Incremental_CRT_System_Nov64.pdf p 2-14 */
#define CONO_MASK       0000077         /* bits changed by CONO */
#define CONI_MASK       0007677         /* bits read by CONI */

#define CONI_INT_SPEC   0007400         /* I- "special conditions" */
#define CONI_INT_VE     0004000         /* I- b24: VER EDGE */
#define CONI_INT_LP     0002000         /* I- b25: LIGHT PEN */
#define CONI_INT_HE     0001000         /* I- b26: HOR EDGE */
#define CONI_INT_SI     0000400         /* I- b27: STOP INT */
#define CONI_INT_DONE   0000200         /* I- b28: done with second half */
#define CONO_RESUME     0000200         /* -O b28: resume after special int */
#define CONO_INIT       0000100         /* -O b29: init display */
#define CONX_SC         0000070         /* IO special channel */
#define CONX_DC         0000007         /* IO data channel */

#define CONX_SC_SHIFT   3
#define CONX_DC_SHIFT   0

/* make sure ST340_XXX bits match CONI_INT_XXX bits */
#if (ST340_VEDGE^CONI_INT_VE)|(ST340_LPHIT^CONI_INT_LP)|(ST340_HEDGE^CONI_INT_HE)|(ST340_STOP_INT^CONI_INT_SI)
#error ST340 bits do not match CONI_INT bits!!
#endif

t_stat dpy_devio(uint32 dev, uint64 *data);
t_stat dpy_svc (UNIT *uptr);
t_stat dpy_reset (DEVICE *dptr);
const char *dpy_description (DEVICE *dptr);

DIB dpy_dib[] = {
        { DPY_DEVNUM, 1, &dpy_devio, NULL }};

UNIT dpy_unit[] = {
        { UDATA (&dpy_svc, UNIT_IDLE, DPY_CYCLE_US) }
};

#define UPTR(UNIT) (dpy_unit+(UNIT))

DEVICE dpy_dev = {
    "DPY", dpy_unit, NULL, NULL,
    NUM_DEVS_DPY, 0, 0, 0, 0, 0,
    NULL, NULL, dpy_reset,
    NULL, NULL, NULL,
    &dpy_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_DISPLAY, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, &dpy_description
    };

const char *dpy_description (DEVICE *dptr)
{
    return "Type 340 Display on Type 344 interface";
}

/* until it's done just one place! */
static void dpy_set_int_done(UNIT *uptr)
{
    uptr->INT_COUNTDOWN = INT_COUNT;
}

/* update interrupt request */
static void check_interrupt (UNIT *uptr)
{
    if (uptr->STAT_REG & CONI_INT_SPEC) {
        uint32 sc = uptr->STAT_REG & CONX_SC;
        set_interrupt(DPY_DEVNUM, sc >> CONX_SC_SHIFT);
    } else if (uptr->STAT_REG & CONI_INT_DONE) {
        uint32 dc = uptr->STAT_REG & CONX_DC;
        set_interrupt(DPY_DEVNUM, dc>>CONX_DC_SHIFT);
    } else {
        clr_interrupt(DPY_DEVNUM);
    }
}

/* return true if display not stopped */
int dpy_update_status (UNIT *uptr, ty340word status, int done)
{
    int running = !(status & ST340_STOPPED);

    /* sub in lastest bits from display */
    uptr->STAT_REG &= ~CONI_INT_SPEC;
    uptr->STAT_REG |= status & CONI_INT_SPEC;

    /* data interrupt sent from svc routine, so CPU can run */
    if (done && running) {
        /* XXX also set in "rfd" callback: decide! */
        dpy_set_int_done(uptr);
    }
    check_interrupt(uptr);
    return running;
}


t_stat dpy_devio(uint32 dev, uint64 *data) {
    int         unit = (dev - DPY_DEVNUM) >> 2;
    UNIT        *uptr;
    int32       inst;

    if (unit < 0 || unit >= NUM_DEVS_DPY)
        return SCPE_OK;
    uptr = UPTR(unit);

    if (!(uptr->STAT_REG & STAT_VALID)) {
        dpy_update_status(uptr, ty340_status(), 0);
        sim_activate_after(uptr, DPY_CYCLE_US);
        uptr->STAT_REG |= STAT_VALID;
        uptr->INT_COUNTDOWN = 0;
    }

    switch (dev & 3) {
    case CONI:
        *data = (uint64)(uptr->STAT_REG & CONI_MASK);
        /*
         * MIT AI only, See Hardware Memo 1
         * https://github.com/larsbrinkhoff/its-archives/blob/master/ailab/ITS_Hardware_Memo_1.pdf
         * Set sign bit if device assigned to this CPU (KA or PDP-6)
         * (Thanks to Lars for figuring this out!)
         */
        *data |= SMASK;                 /* always assigned to us */
        sim_debug(DEBUG_CONI, &dpy_dev, "DPY  %03o CONI PC=%06o %012llo\n",
                  dev, PC, *data);
        break;

    case CONO:
        clr_interrupt(dev);
        uptr->STAT_REG &= ~CONO_MASK;
        uptr->STAT_REG |= *data & CONO_MASK;
        if (*data & CONO_INIT)
            dpy_update_status( uptr, ty340_reset(&dpy_dev), 1);
        if (*data & CONO_RESUME) {
            /* This bit is not documented in "H-340 Type 340 Precision
               Incremental CRT System".  It is in the MIT file .INFO.;
               340 INFO, and ITS does depend on it. */
            ty340_clear(CONI_INT_VE | CONI_INT_LP | CONI_INT_HE);
            dpy_update_status( uptr, ty340_status(), 0);
        }
        sim_debug(DEBUG_CONO, &dpy_dev, "DPY %03o CONO %06o PC=%06o %06o\n",
                  dev, (uint32)*data, PC, uptr->STAT_REG & ~STAT_VALID);
        if (!sim_is_active(uptr))
            sim_activate_after(uptr, DPY_CYCLE_US);
        break;

    case DATAO:
        uptr->STAT_REG &= ~CONI_INT_DONE;
        uptr->INT_COUNTDOWN = 0;

        /* if fed using BLKO from interrupt vector, PC will be wrong! */
        sim_debug(DEBUG_DATAIO, &dpy_dev, "DPY %03o DATO %012llo PC=%06o\n",
                  dev, *data, PC);

        inst = (uint32)LRZ(*data);
        if (dpy_update_status(uptr, ty340_instruction(inst), 0)) {
            /* still running */
            inst = (uint32)RRZ(*data);
            dpy_update_status(uptr, ty340_instruction(inst), 1);
        }
        if (!sim_is_active(uptr))
            sim_activate_after(uptr, DPY_CYCLE_US);
        break;

    case DATAI:
        *data = XWD(uptr->YPOS, uptr->XPOS);
        sim_debug(DEBUG_DATAIO, &dpy_dev, "DPY %03o DATI %06o,,%06o PC=%06o\n",
                  dev, uptr->YPOS, uptr->XPOS, PC);
        break;
    }
    return SCPE_OK;
}

/* Timer service - */
t_stat dpy_svc (UNIT *uptr)
{
    if (!display_is_blank() || uptr->INT_COUNTDOWN > 0)
        sim_activate_after(uptr, DPY_CYCLE_US); /* requeue! */

    display_age(DPY_CYCLE_US, 0);       /* age the display */

    if (uptr->INT_COUNTDOWN && --uptr->INT_COUNTDOWN == 0) {
        uptr->STAT_REG |= CONI_INT_DONE;
        check_interrupt (uptr);
    }
    return SCPE_OK;
}

#define JOY_MAX_UNITS     4
#define JOY_MAX_AXES      4
#define JOY_MAX_BUTTONS   4

static int joy_axes[JOY_MAX_UNITS * JOY_MAX_AXES];
static int joy_buttons[JOY_MAX_UNITS * JOY_MAX_BUTTONS];

static void dpy_joy_motion(int which, int axis, int value)
{
  if (which < JOY_MAX_UNITS && axis < JOY_MAX_AXES) {
    joy_axes[which * JOY_MAX_AXES + axis] = value;
  }
}

static void dpy_joy_button(int which, int button, int state)
{
  if (which < JOY_MAX_UNITS && button < JOY_MAX_BUTTONS) {
    joy_buttons[which * JOY_MAX_UNITS + button] = state;
  }
}

/* Reset routine */

t_stat dpy_reset (DEVICE *dptr)
{
    if (dptr->flags & DEV_DIS) {
        display_close(dptr);
    } else {
        display_reset();
        ty340_reset(dptr);
        vid_register_gamepad_motion_callback (dpy_joy_motion);
        vid_register_gamepad_button_callback (dpy_joy_button);
    }
    sim_cancel (&dpy_unit[0]);             /* deactivate unit */
    return SCPE_OK;
}

/****************
 * callbacks from type340.c
 */

/* not used with Type 344 interface */
ty340word
ty340_fetch(ty340word addr)
{
    return 0;
}

/* not used with Type 344 interface */
void
ty340_store(ty340word addr, ty340word value)
{
}

void
ty340_lp_int(ty340word x, ty340word y)
{
    /*
     * real hardware pauses display until the CPU reads out coords
     * w/ DATAI which then continues the display
     */
    dpy_unit[0].XPOS = x;
    dpy_unit[0].YPOS = y;
    dpy_update_status(dpy_unit, ty340_status(), 0);
}

void
ty340_rfd(void) {                       /* request for data */
#ifdef TY340_NODISPLAY
    puts("ty340_rfd");
#endif
    dpy_set_int_done(dpy_unit);
}

void
cpu_get_switches(unsigned long *p1, unsigned long *p2) {
#if PDP6 | KA | KI
    *p1 = LRZ(SW);
    *p2 = RRZ(SW);
#endif
}

void
cpu_set_switches(unsigned long w1, unsigned long w2) {
#if PDP6 | KA | KI
    SW = XWD(w1,w2);
#endif
}

/*
 * MIT Spacewar console switches
 * WCNSLS is the mnemonic defined/used in the SPCWAR sources
 */
#if NUM_DEVS_WCNSLS > 0
#define WCNSLS_DEVNUM 0420

#define UNIT_JOY      (1 << DEV_V_UF)

t_stat wcnsls_devio(uint32 dev, uint64 *data);
const char *wcnsls_description (DEVICE *dptr);

DIB wcnsls_dib[] = {
    { WCNSLS_DEVNUM, 1, &wcnsls_devio, NULL }};

MTAB wcnsls_mod[] = {
    { UNIT_JOY, UNIT_JOY, "JOYSTICK", "JOYSTICK", NULL, NULL, NULL,
      "Use USB joysticks"},
    { 0 }
    };

UNIT wcnsls_unit[] = {
    { UDATA (NULL, UNIT_IDLE, 0) }};

DEVICE wcnsls_dev = {
    "WCNSLS", wcnsls_unit, NULL, wcnsls_mod,
    NUM_DEVS_WCNSLS, 0, 0, 0, 0, 0,
    NULL, NULL, NULL,
    NULL, NULL, NULL,
    &wcnsls_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, &wcnsls_description
    };

const char *wcnsls_description (DEVICE *dptr)
{
    return "MIT Spacewar Consoles";
}

/*
 * map 32-bit "spacewar_switches" value to what PDP-6/10 game expects
 * (four 9-bit bytes)
 */
/* bits inside the bytes */
#define CCW     0400LL                  /* counter clockwise (L) */
#define CW      0200LL                  /* clockwise (R) */
#define THRUST  0100LL
#define HYPER   040LL
#define FIRE    020LL

/* shift values for the players' bytes */
#define UR      0               /* upper right: enterprise "top plug" */
#define LR      9               /* lower right: klingon "second plug" */
#define LL      18              /* lower left: thin ship "third plug" */
#define UL      27              /* upper left: fat ship "bottom plug" */

#define JOY_TRIG   5000
#define JOY0       (JOY_MAX_AXES*0)
#define JOY1       (JOY_MAX_AXES*1)
#define JOY2       (JOY_MAX_AXES*2)
#define JOY3       (JOY_MAX_AXES*3)
#define BUT0       (JOY_MAX_BUTTONS*0)
#define BUT1       (JOY_MAX_BUTTONS*1)
#define BUT2       (JOY_MAX_BUTTONS*2)
#define BUT3       (JOY_MAX_BUTTONS*3)

static uint64 joystick_switches (void)
{
  uint64 switches = 0777777777777LL;

  if (joy_axes[JOY0] > JOY_TRIG)
    switches &= ~(CCW << UR);
  else if (joy_axes[JOY0] < -JOY_TRIG)
    switches &= ~(CW << UR);
  if (joy_axes[JOY0+1] < -JOY_TRIG)
    switches &= ~(THRUST << UR);
  if (joy_buttons[BUT0])
    switches &= ~(FIRE << UR);
  if (joy_buttons[BUT0+1])
    switches &= ~(HYPER << UR);

  if (joy_axes[JOY1] > JOY_TRIG)
    switches &= ~(CCW << LR);
  else if (joy_axes[JOY1] < -JOY_TRIG)
    switches &= ~(CW << LR);
  if (joy_axes[JOY1+1] < -JOY_TRIG)
    switches &= ~(THRUST << LR);
  if (joy_buttons[BUT1])
    switches &= ~(FIRE << LR);
  if (joy_buttons[BUT1+1])
    switches &= ~(HYPER << LR);

  if (joy_axes[JOY2] > JOY_TRIG)
    switches &= ~(CCW << LL);
  else if (joy_axes[JOY2] < -JOY_TRIG)
    switches &= ~(CW << LL);
  if (joy_axes[JOY2+1] < -JOY_TRIG)
    switches &= ~(THRUST << LL);
  if (joy_buttons[BUT2])
    switches &= ~(FIRE << LL);
  if (joy_buttons[BUT2+1])
    switches &= ~(HYPER << LL);

  if (joy_axes[JOY3] > JOY_TRIG)
    switches &= ~((uint64)CCW << UL);
  else if (joy_axes[JOY3] < -JOY_TRIG)
    switches &= ~((uint64)CW << UL);
  if (joy_axes[JOY3+1] < -JOY_TRIG)
    switches &= ~((uint64)THRUST << UL);
  if (joy_buttons[BUT3])
    switches &= ~((uint64)FIRE << UL);
  if (joy_buttons[BUT3+1])
    switches &= ~(HYPER << UL);

  return switches;
}

static uint64 keyboard_switches (void)
{
    uint64 switches = 0777777777777LL;    /* 1 is off */

#if 1
#define DEBUGSW(X) (void)0
#else
#define DEBUGSW(X) printf X
#endif

#define SWSW(UC, LC, BIT, POS36, FUNC36) \
        if (spacewar_switches & BIT) {                  \
            switches &= ~(((uint64)FUNC36)<<POS36);     \
            DEBUGSW(("mapping %#o %s %s to %03o<<%d\r\n", \
                    (uint32)BIT, #POS36, #FUNC36, FUNC36, POS36)); \
        }
        SPACEWAR_SWITCHES;
#undef SWSW

    if (spacewar_switches)
        DEBUGSW(("in %#lo out %#llo\r\n", spacewar_switches, switches));


    return switches;
}

t_stat wcnsls_devio(uint32 dev, uint64 *data) {
    switch (dev & 3) {
    case CONO:
        /* CONO WCNSLS,40       ;enable spacewar consoles */
        break;

    case DATAI:
        if (wcnsls_unit->flags & UNIT_JOY) {
          *data = joystick_switches ();
        } else {
          *data = keyboard_switches ();
        }

        sim_debug(DEBUG_DATAIO, &wcnsls_dev, "WCNSLS %03o DATI %012llo PC=%06o\n",
                  dev, *data, PC);
        break;
    }
    return SCPE_OK;
}

/*
 * Old MIT Spacewar console switches
 */
#if NUM_DEVS_OCNSLS > 0
#define OCNSLS_DEVNUM 0724

t_stat ocnsls_devio(uint32 dev, uint64 *data);
const char *ocnsls_description (DEVICE *dptr);

DIB ocnsls_dib[] = {
    { OCNSLS_DEVNUM, 1, &ocnsls_devio, NULL }};

UNIT ocnsls_unit[] = {
    { UDATA (NULL, UNIT_IDLE, 0) }};

DEVICE ocnsls_dev = {
    "OCNSLS", ocnsls_unit, NULL, NULL,
    NUM_DEVS_OCNSLS, 0, 0, 0, 0, 0,
    NULL, NULL, NULL,
    NULL, NULL, NULL,
    &ocnsls_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, &ocnsls_description
    };

const char *ocnsls_description (DEVICE *dptr)
{
    return "Old MIT Spacewar Consoles";
}

#define OHYPER  0004LL          /* Hyperspace. */
#define OFIRE   0010LL          /* Fire torpedo. */
#define OCW     0020LL          /* Turn clockwise. */
#define OCCW    0040LL          /* Turn counter clockwise. */
#define SLOW    0100LL          /* Weak thrust. */
#define FAST    0200LL          /* Strong thrust. */
#define BEACON  020000LL        /* Aiming beacon. */

static uint64 old_switches (void)
{
  uint64 switches = 0;

  if (joy_axes[JOY0] > JOY_TRIG)
    switches |= OCCW;
  else if (joy_axes[JOY0] < -JOY_TRIG)
    switches |= OCW;
  if (joy_axes[JOY0+1] < -JOY_TRIG)
    switches |= FAST;
  if (joy_axes[JOY0+1] > JOY_TRIG)
    switches |= SLOW;
  if (joy_buttons[BUT0])
    switches |= OFIRE;
  if (joy_buttons[BUT0+1])
    switches |= OHYPER;
  if (joy_buttons[BUT0+2])
    switches |= BEACON;

  if (joy_axes[JOY1] > JOY_TRIG)
    switches |= OCCW << 18;
  else if (joy_axes[JOY1] < -JOY_TRIG)
    switches |= OCW << 18;
  if (joy_axes[JOY1+1] < -JOY_TRIG)
    switches |= FAST << 18;
  if (joy_axes[JOY1+1] > JOY_TRIG)
    switches |= SLOW << 18;
  if (joy_buttons[BUT1])
    switches |= OFIRE << 18;
  if (joy_buttons[BUT1+1])
    switches |= OHYPER << 18;
  if (joy_buttons[BUT1+2])
    switches |= BEACON << 18;

  return switches;
}

t_stat ocnsls_devio(uint32 dev, uint64 *data) {
    switch (dev & 3) {
    case DATAI:
        *data = old_switches ();
        break;
    case CONI:
        *data = 0;
        break;
    }
    return SCPE_OK;
}
#endif
#endif
#endif
