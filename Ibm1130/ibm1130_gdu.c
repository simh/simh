#include "ibm1130_defs.h"

/* ibm1130_gdu.c: IBM 1130 2250 Graphical Display Unit

   (Under construction)
   stuff to fix:
   "store revert" might be backwards?
   alpha keyboard is not implemented
   pushbuttons are not implemented
   there is something about interrupts being deferred during a subroutine transition?

   Based on the SIMH package written by Robert M Supnik

 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org
 */

#define BLIT_MODE                   /* define for better performance, undefine when debugging generate_image() */
/* #define DEBUG_LIGHTPEN */        /* define to debug light-pen sensing */

#define DEFAULT_GDU_RATE      20    /* default frame rate */
#define DEFAULT_PEN_THRESHOLD  3    /* default looseness of light-pen hit */
#define INDWIDTH              32    /* width of an indicator (there are two columns of these) */
#define INITSIZE             512    /* initial window size */

#define GDU_DSW_ORDER_CONTROLLED_INTERRUPT  0x8000
#define GDU_DSW_KEYBOARD_INTERUPT           0x4000
#define GDU_DSW_DETECT_INTERRUPT            0x2000
#define GDU_DSW_CYCLE_STEAL_CHECK           0x1000
#define GDU_DSW_DETECT_STATUS               0x0800
#define GDU_DSW_LIGHT_PEN_SWITCH            0x0100
#define GDU_DSW_BUSY                        0x0080
#define GDU_DSW_CHARACTER_MODE              0x0040
#define GDU_DSW_POINT_MODE                  0x0020
#define GDU_DSW_ADDR_DISP                   0x0003

#define GDU_FKEY_DATA_AVAILABLE             0x8000
#define GDU_FKEY_KEY_CODE                   0x1F00
#define GDU_FKEY_OVERLAY_CODE               0x00FF

#define GDU_AKEY_DATA_AVAILABLE             0x8000
#define GDU_AKEY_END                        0x1000
#define GDU_AKEY_CANCEL                     0x0800
#define GDU_AKEY_ADVANCE                    0x0400
#define GDU_AKEY_BACKSPACE                  0x0200
#define GDU_AKEY_JUMP                       0x0100
#define GDU_AKEY_KEY_CODE                   0x00FF

/* -------------------------------------------------------------------------------------- */

#define UNIT_V_DISPLAYED            (UNIT_V_UF + 0)
#define UNIT_V_DETECTS_ENABLED      (UNIT_V_UF + 1)
#define UNIT_V_INTERRUPTS_DEFERRED  (UNIT_V_UF + 2)
#define UNIT_V_LARGE_CHARS          (UNIT_V_UF + 3)

#define UNIT_DISPLAYED              (1u << UNIT_V_DISPLAYED)            /* display windows is up */
#define UNIT_DETECTS_ENABLED        (1u << UNIT_V_DETECTS_ENABLED)      /* light pen detects are enabled */
#define UNIT_INTERRUPTS_DEFERRED    (1u << UNIT_V_INTERRUPTS_DEFERRED)  /* light pen interrupts are deferred */
#define UNIT_LARGE_CHARS            (1u << UNIT_V_LARGE_CHARS)          /* large character mode */

static t_stat gdu_reset (DEVICE *dptr);

static int16 gdu_dsw  = 1;                                  /* device status word */
static int16 gdu_ar   = 0;                                  /* address register */
static int16 gdu_x    = 0;                                  /* X deflection */
static int16 gdu_y    = 0;                                  /* Y deflection */
static int16 gdu_fkey = 0;                                  /* function keyboard register */
static int16 gdu_akey = 0;                                  /* alphanumeric keyboard register */
static int16 gdu_revert = 0;                                /* revert address register */
static int32 gdu_indicators = 0;                            /* programmed indicator lamps */
static int32 gdu_threshold = DEFAULT_PEN_THRESHOLD;         /* mouse must be within 3/1024 of line to be a hit */
static int32 gdu_rate = DEFAULT_GDU_RATE;                   /* refresh rate. 0 = default */

UNIT gdu_unit = { UDATA (NULL, 0, 0) };

REG gdu_reg[] = {
    { HRDATA (GDUDSW,   gdu_dsw,        16) },              /* device status word */
    { HRDATA (GDUAR,    gdu_ar,         16) },              /* address register */
    { HRDATA (GDUXREG,  gdu_x,          16) },              /* X deflection register */
    { HRDATA (GDUYREG,  gdu_y,          16) },              /* Y deflection register */
    { HRDATA (GDUFKEY,  gdu_fkey,       16) },              /* function keyboard register */
    { HRDATA (GDUAKEY,  gdu_akey,       16) },              /* alphanumeric keyboard register */
    { HRDATA (GDUREVERT,gdu_revert,     16) },              /* revert address register */
    { HRDATA (GDUAKEY,  gdu_indicators, 32) },              /* programmed indicators */
    { DRDATA (GDUTHRESH,gdu_threshold,  32) },              /* mouse closeness threshhold */
    { DRDATA (GDURATE,  gdu_rate,       32) },              /* refresh rate in frames/sec */
    { NULL }  };

DEVICE gdu_dev = {
    "GDU", &gdu_unit, gdu_reg, NULL,
    1, 16, 16, 1, 16, 16,
    NULL, NULL, gdu_reset,
    NULL, NULL, NULL};

/* -------------------------------------------------------------------------------------- */

#ifndef GUI_SUPPORT

static t_stat gdu_reset (DEVICE *dptr)
{
    return SCPE_OK;
}

void xio_2250_display (int32 addr, int32 func, int32 modify)
{
    /* ignore commands if device is nonexistent */
}

t_bool gdu_active (void)
{
    return 0;
}

/* -------------------------------------------------------------------------------------- */
#else   /* GUI_SUPPORT defined */

/******* PLATFORM INDEPENDENT CODE ********************************************************/

static int32 gdu_instaddr;                          // address of first word of instruction
static int xmouse, ymouse, lpen_dist, lpen_dist2;   // current mouse pointer, scaled closeness threshhold, same squared
static double sfactor;                              // current scaling factor
static t_bool last_abs = TRUE;                      // last positioning instruction was absolute
static t_bool mouse_present = FALSE;                // mouse is/is not in the window
static void clear_interrupts (void);
static void set_indicators (int32 new_inds);
static void start_regeneration (void);
static void halt_regeneration (void);
static void draw_characters (void);
static void notify_window_closed (void);

// routines that must be implemented per-platform

static void   DrawLine(int x0, int y0, int x1, int y1);
static void   DrawPoint(int x, int y);
static void   CheckGDUKeyboard(void);
static t_bool CreateGDUWindow(void);
static void   StartGDUUpdates(void);
static void   StopGDUUpdates(void);
static void   GetMouseCoordinates(void);
static void   UpdateGDUIndicators(void);
static void   ShowPenHit (int x, int y);
static void   EraseGDUScreen (void);

/* -------------------------------------------------------------------------------------- */

void xio_2250_display (int32 addr, int32 func, int32 modify)
{
    if (cgi) return;                                /* ignore this device in CGI mode */

    switch (func) {
        case XIO_SENSE_DEV:
            ACC = (gdu_dsw & GDU_DSW_BUSY) ? GDU_DSW_BUSY : gdu_dsw;
            if (modify & 1)
                clear_interrupts();
            break;

        case XIO_READ:                              /* store status data into word pointed to by IOCC packet */
            if (gdu_dsw & GDU_DSW_BUSY)             /* not permitted while device is busy */
                break;

            WriteW(addr,   gdu_ar);                 /* save status information */
            WriteW(addr+1, gdu_dsw);
            WriteW(addr+2, gdu_x & 0x7FF);
            WriteW(addr+3, gdu_y & 0x7FF);
            WriteW(addr+4, gdu_fkey);
            WriteW(addr+5, gdu_akey);
            gdu_ar = (int16) (addr+6);              /* this alters the channel address register? */

            clear_interrupts();                     /* read status clears the interrupts */
            break;

        case XIO_WRITE:
            if (gdu_dsw & GDU_DSW_BUSY)             /* treated as no-op if display is busy */
                break;

            if (modify & 0x80) {                    /* bit 8 on means set indicators, 0 means start regeneration */
                set_indicators((ReadW(addr) << 16) | ReadW(addr+1));
            }
            else {
                gdu_ar   = (int16) addr;
                gdu_fkey = 0;
                gdu_akey = 0;
                clear_interrupts();
                start_regeneration();
            }
            break;

        case XIO_CONTROL:
            if (modify & 0x80) {                    /* bit 8 on means reset, off is no-op */
                gdu_reset(&gdu_dev);
                set_indicators((addr << 16) | addr);
            }
            break;

        default:                                    /* all other commands are no-ops */
            break;
    }
}

static t_stat gdu_reset (DEVICE *dptr)
{
    if (cgi) return SCPE_OK;                            /* ignore this device in CGI mode */

    halt_regeneration();
    clear_interrupts();
    set_indicators(0);
    gdu_x = gdu_y = 512;
    CLRBIT(gdu_unit.flags, UNIT_INTERRUPTS_DEFERRED | UNIT_DETECTS_ENABLED | UNIT_LARGE_CHARS);
    gdu_dsw = 1;
    return SCPE_OK;
}

static void clear_interrupts (void)
{
    CLRBIT(gdu_dsw, GDU_DSW_ORDER_CONTROLLED_INTERRUPT | GDU_DSW_KEYBOARD_INTERUPT | GDU_DSW_DETECT_INTERRUPT);
    CLRBIT(ILSW[3], ILSW_3_2250_DISPLAY);
    calc_ints();
}

static void gdu_interrupt (int32 dswbit)
{
    SETBIT(gdu_dsw, dswbit);
    SETBIT(ILSW[3], ILSW_3_2250_DISPLAY);
    calc_ints();
    halt_regeneration();
}

static void set_indicators (int32 new_inds)
{
    gdu_indicators = new_inds;
    if (gdu_unit.flags & UNIT_DISPLAYED)
        UpdateGDUIndicators();
}

static void start_regeneration (void)
{
    SETBIT(gdu_dsw, GDU_DSW_BUSY);

    if ((gdu_unit.flags & UNIT_DISPLAYED) == 0) {
        if (! CreateGDUWindow())
            return;

        SETBIT(gdu_unit.flags, UNIT_DISPLAYED);
    }

    StartGDUUpdates();
}

static void halt_regeneration (void)
{
    // halt_regeneration gets called at end of every refresh interation, so it should NOT black out the
    // screen -- this is why it was flickering so badly. The lower level code (called on a timer)
    // should check to see if GDU_DSW_BUSY is clear, and if it it still zero after several msec,
    // only then should it black out the screen and call StopGDUUpdates.
    if (gdu_dsw & GDU_DSW_BUSY) {
        CLRBIT(gdu_dsw, GDU_DSW_BUSY);
    }
}

static void notify_window_closed (void)
{
    if (gdu_dsw & GDU_DSW_BUSY) {
        StopGDUUpdates();
        CLRBIT(gdu_dsw, GDU_DSW_BUSY);
    }

    CLRBIT(gdu_unit.flags, UNIT_DISPLAYED);

    gdu_reset(&gdu_dev);
}

static int32 read_gduword (void)
{
    int32 w;

    w = M[gdu_ar++ & mem_mask];
    gdu_dsw = (int16) ((gdu_dsw & ~GDU_DSW_ADDR_DISP) | ((gdu_ar - gdu_instaddr) & GDU_DSW_ADDR_DISP));

    return w;
}

#define DIST2(x0,y0,x1,y1) (((x1)-(x0))*((x1)-(x0))+((y1)-(y0))*((y1)-(y0)))

static void draw (int32 newx, int32 newy, t_bool beam)
{
    int xmin, xmax, ymin, ymax, xd, yd;
    double s;
    int hit = FALSE;

    if (beam) {
        if (gdu_dsw & GDU_DSW_POINT_MODE) {
            DrawPoint(newx, newy);

#ifdef DEBUG_LIGHTPEN
            if (DIST2(newx, newy, xmouse, ymouse) <= lpen_dist2)
                hit = TRUE;
#else
            if (gdu_unit.flags & UNIT_DETECTS_ENABLED && mouse_present)
                if (DIST2(newx, newy, xmouse, ymouse) <= lpen_dist2)
                    hit = TRUE;
#endif
        }
        else {
            DrawLine(gdu_x, gdu_y, newx, newy);

            // calculate proximity of light pen to the line
#ifndef DEBUG_LIGHTPEN
            if (gdu_unit.flags & UNIT_DETECTS_ENABLED && mouse_present) {
#endif
                if (gdu_x <= newx)
                    xmin = gdu_x, xmax = newx;
                else
                    xmin = newx,  xmax = gdu_x;

                if (gdu_y <= newy)
                    ymin = gdu_y, ymax = newy;
                else
                    ymin = newy,  ymax = gdu_y;

                if (newx == gdu_x) {
                    // line is vertical. Nearest point is an endpoint if the mouse is above or
                    // below the line segment, otherwise the segment point at the same y as the mouse
                    xd = gdu_x;
                    yd = (ymouse <= ymin) ? ymin : (ymouse >= ymax) ? ymax : ymouse;

                    if (DIST2(xd, yd, xmouse, ymouse) <= lpen_dist2)
                        hit = TRUE;
                }
                else if (newy == gdu_y) {
                    // line is horizontal. Nearest point is an endpoint if the mouse is to the left or
                    // the right of the line segment, otherwise the segment point at the same x as the mouse
                    xd = (xmouse <= xmin) ? xmin : (xmouse >= xmax) ? xmax : xmouse;
                    yd = gdu_y;

                    if (DIST2(xd, yd, xmouse, ymouse) <= lpen_dist2)
                        hit = TRUE;
                }
                else {
                    // line is diagonal. See if the mouse is inside the box lpen_dist wider than the line segment's bounding rectangle
                    if (xmouse >= (xmin-lpen_dist) && xmouse <= (xmax+lpen_dist) && ymouse >= (ymin-lpen_dist) || ymouse <= (ymax+lpen_dist)) {
                        // compute the point at the intersection of the line through the line segment and the normal
                        // to that line through the mouse. This is the point on the line through the line segment
                        // nearest the mouse

                        s  = (double)(newy - gdu_y) / (double)(newx - gdu_x);       // slope of line segment
                        xd = (int) ((ymouse + xmouse/s - gdu_y + s*gdu_x) / (s + 1./s) + 0.5);

                        // if intersection is beyond either end of the line segment, the nearest point to the
                        // mouse is nearest segment end, otherwise it's the computed intersection point
                        if (xd < xmin || xd > xmax) {
#ifdef DEBUG_LIGHTPEN
                            // if it's a hit, set xd and yd so we can display the hit
                            if (DIST2(gdu_x, gdu_y, xmouse, ymouse) <= lpen_dist2) {
                                hit = TRUE;
                                xd = gdu_x;
                                yd = gdu_y;
                            }
                            else if (DIST2(newx, newy, xmouse, ymouse) <= lpen_dist2) {
                                hit = TRUE;
                                xd = newx;
                                yd = newy;
                            }
#else
                            if (DIST2(gdu_x, gdu_y, xmouse, ymouse) <= lpen_dist2 || DIST2(newx, newy, xmouse, ymouse) <= lpen_dist2)
                                hit = TRUE;
#endif
                        }
                        else {
                            yd = (int) (gdu_y + s*(xd - gdu_x) + 0.5);
                            if (DIST2(xd, yd, xmouse, ymouse) <= lpen_dist2)
                                hit = TRUE;
                        }
                    }
                }
#ifndef DEBUG_LIGHTPEN
            }
#endif
        }
    }

    if (hit) {
#ifdef DEBUG_LIGHTPEN
        ShowPenHit(xd, yd);
        if (gdu_unit.flags & UNIT_DETECTS_ENABLED && mouse_present)
            SETBIT(gdu_dsw, GDU_DSW_DETECT_STATUS);
#else
        SETBIT(gdu_dsw, GDU_DSW_DETECT_STATUS);
#endif
    }

    gdu_x = (int16) newx;
    gdu_y = (int16) newy;
}

static void generate_image (void)
{
    int32 instr, new_addr, newx, newy;
    t_bool run = TRUE, accept;

    if (! (gdu_dsw & GDU_DSW_BUSY))
        return;

    GetMouseCoordinates();

    lpen_dist  = (int) (gdu_threshold/sfactor + 0.5);   // mouse-to-line threshhold at current scaling factor
    lpen_dist2 = lpen_dist * lpen_dist;

    while (run) {
        if ((gdu_dsw & GDU_DSW_DETECT_STATUS) && ! (gdu_unit.flags & UNIT_INTERRUPTS_DEFERRED)) {
            CLRBIT(gdu_dsw, GDU_DSW_DETECT_STATUS); // clear when interrupt is activated
            gdu_interrupt(GDU_DSW_DETECT_INTERRUPT);
            run = FALSE;
            break;
        }

        gdu_instaddr = gdu_ar;                      // remember address of GDU instruction
        instr = read_gduword();                     // fetch instruction (and we really are cycle stealing here!)

        switch ((instr >> 12) & 0xF) {              // decode instruction
            case 0:                                 // short branch
            case 1:
                gdu_revert = gdu_ar;                // save revert address & get new address
                gdu_ar = (int16) (read_gduword() & 0x1FFF);
                if (gdu_dsw & GDU_DSW_CHARACTER_MODE) {
                    draw_characters();              // in character mode this means we are at character data
                    gdu_ar = gdu_revert;
                }
                break;

            case 2:                                 // long branch/interrupt
                new_addr = read_gduword();          // get next word
                accept = ((instr & 1) ? (gdu_dsw & GDU_DSW_LIGHT_PEN_SWITCH) : TRUE) && ((instr & 2) ? (gdu_dsw & GDU_DSW_DETECT_STATUS) : TRUE);

                if (instr & 2)                      // clear after testing
                    CLRBIT(gdu_dsw, GDU_DSW_DETECT_STATUS);

                if (instr & 0x0400)                 // NOP
                    accept = FALSE;

                if (accept) {
                    if (instr & 0x0800) {           // branch
                        gdu_revert = gdu_ar;

                        if (instr & 0x0080)         // indirect
                            new_addr = M[new_addr & mem_mask];

                        gdu_ar = (int16) new_addr;

                        if (gdu_dsw & GDU_DSW_CHARACTER_MODE) {
                            draw_characters();
                            gdu_ar = gdu_revert;
                        }
                    }
                    else {                          // interrupt
                        gdu_interrupt(GDU_DSW_ORDER_CONTROLLED_INTERRUPT);
                        run = FALSE;
                    }
                }
                break;

            case 3:                                 // control instructions
                CLRBIT(gdu_dsw, GDU_DSW_CHARACTER_MODE);

                switch ((instr >> 8) & 0xF) {
                    case 1:                         // set pen mode
                        if ((instr & 0xC) == 8)
                            SETBIT(gdu_unit.flags, UNIT_DETECTS_ENABLED);
                        else if ((instr & 0xC) == 4)
                            CLRBIT(gdu_unit.flags, UNIT_DETECTS_ENABLED);

                        if ((instr & 0x3) == 2)
                            SETBIT(gdu_unit.flags, UNIT_INTERRUPTS_DEFERRED);
                        else if ((instr & 0x3) == 1)
                            CLRBIT(gdu_unit.flags, UNIT_INTERRUPTS_DEFERRED);
                        break;

                    case 2:                         // set graphic mode
                        if (instr & 1)
                            SETBIT(gdu_dsw, GDU_DSW_POINT_MODE);
                        else
                            CLRBIT(gdu_dsw, GDU_DSW_POINT_MODE);
                        break;

                    case 3:                         // set character mode
                        SETBIT(gdu_dsw, GDU_DSW_CHARACTER_MODE);
                        if (instr & 1)
                            SETBIT(gdu_unit.flags, UNIT_LARGE_CHARS);
                        else
                            CLRBIT(gdu_unit.flags, UNIT_LARGE_CHARS);
                        break;

                    case 4:                         // start timer
                        run = FALSE;                // (which, for us, means stop processing until next timer message)
                        CheckGDUKeyboard();
                        break;

                    case 5:                         // store revert
                        M[gdu_ar & mem_mask] = gdu_revert;
                        read_gduword();             // skip to next address
                        break;

                    case 6:                         // revert
                        gdu_ar = gdu_revert;
                        break;

                    default:                        // all others treated as no-ops
                        break;
                }
                break;

            case 4:                                 // long absolute
            case 5:
                CLRBIT(gdu_dsw, GDU_DSW_CHARACTER_MODE);
                newx = instr & 0x3FF;
                newy = read_gduword() & 0x3FF;
                draw(newx, newy, instr & 0x1000);
                last_abs = TRUE;
                break;

            case 6:                                 // short absolute
            case 7:
                CLRBIT(gdu_dsw, GDU_DSW_CHARACTER_MODE);
                newx = gdu_x;
                newy = gdu_y;
                if (instr & 0x0800)
                    newy = instr & 0x3FF;
                else
                    newx = instr & 0x3FF;
                draw(newx, newy, instr & 0x1000);
                last_abs = TRUE;
                break;

            default:                                // high bit set - it's a relative instruction
                CLRBIT(gdu_dsw, GDU_DSW_CHARACTER_MODE);
                newx = (instr >> 8) & 0x3F;
                newy = instr & 0x3F;

                if (instr & 0x4000)                 // sign extend x - values are in 2's complement
                    newx |= -1 & ~0x3F;             // although documentation doesn't make that clear

                if (instr & 0x0040)                 // sign extend y
                    newy |= -1 & ~0x3F;

                newx = gdu_x + newx;
                newy = gdu_y + newy;
                draw(newx, newy, instr & 0x0080);
                last_abs = FALSE;
                break;
        }
    }
}

static struct charinfo {        // character mode scaling info:
    int dx, dy;                     // character and line spacing
    double sx, sy;                  // scaling factors: character units to screen units
    int xoff, yoff;                 // x and y offset to lower left corner of character
    int suby;                       // subscript/superscript offset
} cx[2] = {
    {14, 20, 1.7, 2.0, -6, -7,  6}, // regular
    {21, 30, 2.5, 3.0, -9, -11, 9}  // large
};

static void draw_characters (void)
{
    int32 w, x0, y0, x1, y1, yoff = 0, ninstr = 0;
    t_bool dospace, didstroke = FALSE;
    struct charinfo *ci;

    ci = &cx[(gdu_unit.flags & UNIT_LARGE_CHARS) ? 1 : 0];
    x0 = gdu_x + ci->xoff;                      // starting position
    y0 = gdu_y + ci->yoff;

    do {
        if (++ninstr > 29) {                    // too many control words
            gdu_interrupt(GDU_DSW_CYCLE_STEAL_CHECK);
            return;
        }

        dospace = TRUE;
        w = M[gdu_ar++ & mem_mask];             // get next stroke or control word

        x1 = (w >> 12) & 7;
        y1 = (w >>  8) & 7;

        if (x1 == 7) {                          // this is a character control word
            dospace = FALSE;                    // inhibit character spacing

            switch (y1) {
                case 1:                         // subscript
                    if (yoff == 0)              // (ignored if superscript is in effect)
                        yoff = -ci->suby;
                    break;

//              case 2:                         // no-op or null (nothing to do)
//              default:                        // all unknowns are no-ops
//                  break;

                case 4:                         // superscript
                    yoff = ci->suby;
                    break;

                case 7:                         // new line
                    gdu_x = 0;
                    gdu_y -= (int16) ci->dy;
                    if (gdu_y < 0 && last_abs)
                        gdu_y = (int16) (1024 - ci->dy);    // this is a guess
                    break;
            }
        }
        else {                                  // this is stroke data -- extract two strokes
            x1 = gdu_x + (int) (x1*ci->sx + 0.5);
            y1 = gdu_y + (int) ((y1+yoff)*ci->sy + 0.5);

            if (w & 0x0800) {
                didstroke = TRUE;
                DrawLine(x0, y0, x1, y1);
            }

            x0 = (w >> 4) & 7;
            y0 =  w       & 7;

            x0 = gdu_x + (int) (x0*ci->sx + 0.5);
            y0 = gdu_y + (int) ((y0+yoff)*ci->sy + 0.5);

            if (w & 0x0008) {
                didstroke = TRUE;
                DrawLine(x1, y1, x0, y0);
            } 
        }

        if (dospace) {
            gdu_x += ci->dx;
            if (gdu_x > 1023 && last_abs) {     // line wrap
                gdu_x = 0;
                gdu_y -= (int16) ci->dy;
            }
        }
    } while ((w & 0x0080) == 0);                // repeat until we hit revert bit

    if (didstroke && mouse_present && (gdu_unit.flags & UNIT_DETECTS_ENABLED)) {
        if (xmouse >= (gdu_x - ci->xoff/2) && xmouse <= (gdu_x + ci->xoff/2) &&
                ymouse >= (gdu_y - ci->yoff/2) && ymouse <= (gdu_y + ci->yoff/2))
            SETBIT(gdu_dsw, GDU_DSW_DETECT_STATUS);
    }
}

/******* PLATFORM SPECIFIC CODE ***********************************************************/

#ifdef _WIN32

#include <windows.h>
#include <windowsx.h>

#define APPCLASS "IBM2250GDU"                   // window class name

#define RGB_GREEN RGB(0,255,0)                  // handy colors
#define RGB_RED   RGB(255,0,0)

static HINSTANCE hInstance;
static HWND hwGDU       = NULL;
static HDC  hdcGDU      = NULL;
static HBITMAP hBmp     = NULL;
static int  curwid      = 0;
static int  curht       = 0;
static BOOL wcInited    = FALSE;
static DWORD GDUPumpID   = 0;
static HANDLE hGDUPump  = INVALID_HANDLE_VALUE;
static HPEN hGreenPen   = NULL;
static HBRUSH hRedBrush = NULL;
#ifdef DEBUG_LIGHTPEN
static HPEN hRedPen     = NULL;
#endif
static HBRUSH hGrayBrush, hDarkBrush;
static HPEN hBlackPen;
static int halted = 0;                              // number of time intervals that GDU has been halted w/o a regeneration
static UINT idTimer = 0;
static t_bool painting = FALSE;
static LRESULT APIENTRY GDUWndProc (HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);
static DWORD   WINAPI   GDUPump (LPVOID arg);

static void destroy_GDU_window (void)
{
    if (hwGDU != NULL)
        SendMessage(hwGDU, WM_CLOSE, 0, 0);         // cross thread call is OK

#ifdef XXXX
    // let window closure do this
    if (hGDUPump != INVALID_HANDLE_VALUE) {         // this is not the most graceful way to do it
        TerminateThread(hGDUPump, 0);
        hGDUPump  = INVALID_HANDLE_VALUE;
        GDUPumpID = 0;
        hwGDU     = NULL;
    }

    if (hdcGDU != NULL) {
        DeleteDC(hdcGDU);
        hdcGDU = NULL;
    }

    if (hBmp != NULL) {
        DeleteObject(hBmp);
        hBmp = NULL;
    }

    if (hGreenPen != NULL) {
        DeleteObject(hGreenPen);
        hGreenPen = NULL;
    }

    if (hRedBrush != NULL) {
        DeleteObject(hRedBrush);
        hRedBrush = NULL;
    }
#endif

#ifdef DEBUG_LIGHTPEN
    if (hRedPen != NULL) {
        DeleteObject(hRedPen);
        hRedPen = NULL;
    }
#endif
}

static t_bool CreateGDUWindow (void)
{
    static BOOL did_atexit = FALSE;

    hInstance = GetModuleHandle(NULL);

    if (hGDUPump == INVALID_HANDLE_VALUE)
        hGDUPump = CreateThread(NULL, 0, GDUPump, 0, 0, &GDUPumpID);

    if (! did_atexit) {
        atexit(destroy_GDU_window);
        did_atexit = TRUE;
    }

    return TRUE;
}

// windows message handlers ----------------------------------------------------

// close the window

static void gdu_WM_CLOSE (HWND hWnd)
{
    DestroyWindow(hWnd);
}

// the window is being destroyed

static void gdu_WM_DESTROY (HWND hWnd)
{
    PostMessage(hWnd, WM_QUIT, 0, 0);
    if (idTimer != 0) {
        KillTimer(hwGDU, 1);
        idTimer = 0;
        halted  = 10000;
        painting = FALSE;
    }
    notify_window_closed();
    hwGDU = NULL;
}

// adjust the min and max resizing boundaries

static void gdu_WM_GETMINMAXINFO (HWND hWnd, LPMINMAXINFO mm)
{
    mm->ptMinTrackSize.x = 100 + 2*INDWIDTH;
    mm->ptMinTrackSize.y = 100;
}

static void PaintImage (HDC hDC, BOOL draw_indicators)
{
    HPEN hOldPen;
    RECT r;
    int wid, ht, x, y, dy, i, j, ycirc;
    unsigned long bit;

    GetClientRect(hwGDU, &r);
    wid = r.right+1 - 2*INDWIDTH;
    ht  = r.bottom+1;
    sfactor = (double) MIN(wid,ht) / 1024.;

    if (gdu_dsw & GDU_DSW_BUSY) {
#ifdef BLIT_MODE
        // if compiled for BLIT_MODE, draw the image into a memory display context, then
        // blit the new image over window. This eliminates the flicker that a normal erase-and-
        // repaint would cause.

        if (wid != curwid || ht != curht) {     // dimensions have changed, discard old memory display context
            if (hdcGDU != NULL) {
                DeleteDC(hdcGDU);
                hdcGDU = NULL;
            }
            curwid = wid;
            curht  = ht;
        }

        if (hdcGDU == NULL) {                   // allocate memory display context & select a bitmap into it
            hdcGDU = CreateCompatibleDC(hDC);
            if (hBmp != NULL)
                DeleteObject(hBmp);
            hBmp = CreateCompatibleBitmap(hDC, wid, ht);
            SelectObject(hdcGDU, hBmp);
        }

        PatBlt(hdcGDU, 0, 0, wid, ht, BLACKNESS);           // start with a black screen

        hOldPen = SelectObject(hdcGDU, hGreenPen);

        SetMapMode(hdcGDU, MM_ANISOTROPIC);
        SetWindowExtEx(hdcGDU, 1024, -1024, NULL);
        SetViewportExtEx(hdcGDU, wid, ht, NULL);
        SetWindowOrgEx(hdcGDU, 0, 1023, NULL);

        generate_image();                                   // run the display program to paint the image into the memory context

        SetWindowExtEx(hdcGDU, wid, ht, NULL);              // undo the scaling so the blit isn't distorted
        SetViewportExtEx(hdcGDU, wid, ht, NULL);
        SetWindowOrgEx(hdcGDU, 0, 0, NULL);
        BitBlt(hDC, 0, 0, wid, ht, hdcGDU, 0, 0, SRCCOPY);  // blit the new image over the old

        SelectObject(hdcGDU, hOldPen);
#else
        // for testing purposes -- draw the image directly onto the screen.
        // Compile this way when you want to single-step through the image drawing routine,
        // so you can see the draws occur.
        hdcGDU = hDC;
        hOldPen = SelectObject(hdcGDU, hGreenPen);

        SetMapMode(hdcGDU, MM_ANISOTROPIC);
        SetWindowExtEx(hdcGDU, 1024, -1024, NULL);
        SetViewportExtEx(hdcGDU, wid, ht, NULL);
        SetWindowOrgEx(hdcGDU, 0, 1023, NULL);

        generate_image();

        SelectObject(hdcGDU, hOldPen);
        hdcGDU = NULL;
#endif
    }

    if (draw_indicators) {
        x  = r.right-2*INDWIDTH+1;
        dy = ht / 16;
        ycirc = MIN(dy-2, 8);

        r.left = x;
        FillRect(hDC, &r, hGrayBrush);
        SelectObject(hDC, hBlackPen);

        bit = 0x80000000L;
        for (i = 0; i < 2; i++) {
            MoveToEx(hDC, x, 0, NULL);
            LineTo(hDC, x, r.bottom);
            y = 0;
            for (j = 0; j < 16; j++) {
                MoveToEx(hDC, x, y, NULL);
                LineTo(hDC, x+INDWIDTH, y);

                SelectObject(hDC, (gdu_indicators & bit) ? hRedBrush : hDarkBrush);
                Pie(hDC, x+1, y+1, x+1+ycirc, y+1+ycirc, x+1, y+1, x+1, y+1);

                y += dy;
                bit >>= 1;
            }
            x += INDWIDTH;
        }
    }
}

// repaint the window

static void gdu_WM_PAINT (HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hDC;
    int msec;

                // code for display
    hDC = BeginPaint(hWnd, &ps);
    PaintImage(hDC, TRUE);
    EndPaint(hWnd, &ps);

                // set a timer so we keep doing it!
    if (idTimer == 0) {
        msec = (gdu_rate == 0) ? (1000 / DEFAULT_GDU_RATE) : 1000/gdu_rate;
        idTimer = SetTimer(hwGDU, 1, msec, NULL);
    }
}

// the window has been resized

static void gdu_WM_SIZE (HWND hWnd, UINT state, int cx, int cy)
{
#ifdef BLIT_MODE
    InvalidateRect(hWnd, NULL, FALSE);      // in blt mode, we'll paint a full black bitmap over the new screen size
#else
    InvalidateRect(hWnd, NULL, TRUE);
#endif
}

// tweak the sizing rectangle during a resize to guarantee a square window

static void gdu_WM_SIZING (HWND hWnd, WPARAM fwSide, LPRECT r)
{
    switch (fwSide) {
        case WMSZ_LEFT:
        case WMSZ_RIGHT:
        case WMSZ_BOTTOMLEFT:
        case WMSZ_BOTTOMRIGHT:
            r->bottom = r->right - r->left - 2*INDWIDTH + r->top;
            break;

        case WMSZ_TOP:
        case WMSZ_BOTTOM:
        case WMSZ_TOPRIGHT:
            r->right  = r->bottom - r->top + r->left + 2*INDWIDTH;
            break;

        case WMSZ_TOPLEFT:
            r->left   = r->top - r->bottom + r->right - 2*INDWIDTH;
            break;
    }
}

// the refresh timer has gone off

static void gdu_WM_TIMER (HWND hWnd, UINT id)
{
    HDC hDC;

    if (painting)       {                       // if GDU is running, update picture
        if ((gdu_dsw & GDU_DSW_BUSY) == 0) {    // regeneration is not to occur
            if (++halted >= 4) {                // stop the timer if four timer intervals go by with the display halted
                EraseGDUScreen();               // screen goes black due to cessation of refreshing
                StopGDUUpdates();               // might as well kill the timer
                return;
            }
        }
        else
            halted = 0;

#ifdef BLIT_MODE
        hDC = GetDC(hWnd);                      // blit the new image right over the old
        PaintImage(hDC, FALSE);
        ReleaseDC(hWnd, hDC);
#else
        InvalidateRect(hWnd, NULL, TRUE);       // repaint
#endif
    }
}

// window procedure ------------------------------------------------------------

#define HANDLE(msg) case msg: return HANDLE_##msg(hWnd, wParam, lParam, gdu_##msg);

#ifndef HANDLE_WM_SIZING
// void Cls_OnSizing(HWND hwnd, UINT fwSide, LPRECT r)
#  define HANDLE_WM_SIZING(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (UINT)(wParam), (LPRECT)(lParam)), 0L)
#endif

static LRESULT APIENTRY GDUWndProc (HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam)
{
    switch (iMessage) {
        HANDLE(WM_CLOSE);
        HANDLE(WM_GETMINMAXINFO);
        HANDLE(WM_DESTROY);
        HANDLE(WM_PAINT);
        HANDLE(WM_SIZE);
        HANDLE(WM_SIZING);
        HANDLE(WM_TIMER);
        default:                    // any message we don't process
            return DefWindowProc(hWnd, iMessage, wParam, lParam);
    }
    return 0L;
}

// graphic calls ----------------------------------------------------------------

static void DrawLine (int x0, int y0, int x1, int y1)
{
    MoveToEx(hdcGDU, x0, y0, NULL);
    LineTo(hdcGDU, x1, y1);
}

static void DrawPoint (int x, int y)
{
    SetPixel(hdcGDU, x, y, RGB_GREEN);
}

static void UpdateGDUIndicators(void)
{
    if (hwGDU != NULL)
        InvalidateRect(hwGDU, NULL, FALSE);         // no need to erase the background -- the draw routine fully paints the indicator
}

static void CheckGDUKeyboard (void)
{
}

static void StartGDUUpdates (void)
{
    halted = 0;
    painting = TRUE;
}

static void StopGDUUpdates (void)
{
    painting = FALSE;
}

static void GetMouseCoordinates()
{
    POINT p;
    RECT r;

    GetCursorPos(&p);
    GetClientRect(hwGDU, &r);
    if (! ScreenToClient(hwGDU, &p)) {
        xmouse = ymouse = -2000;
        mouse_present = FALSE;
        return;
    }

    if (p.x < r.left || p.x >= r.right || p.y < r.top || p.y > r.bottom) {
        mouse_present = FALSE;
        return;
    }

    // convert mouse coordinates to scaled coordinates

    xmouse =        (int) (1024./(r.right+1.-2*INDWIDTH)*p.x + 0.5);
    ymouse = 1023 - (int) (1024./(r.bottom+1.)*p.y + 0.5);
    mouse_present = TRUE;
}

t_bool gdu_active (void)
{
    return cgi ? 0 : (gdu_dsw & GDU_DSW_BUSY);
}

static void EraseGDUScreen (void)
{
    if (hwGDU != NULL)                              /* redraw screen. it will be blank if GDU is not running */
        InvalidateRect(hwGDU, NULL, TRUE);
}

/* GDUPump - thread responsible for creating and displaying the graphics window */

static DWORD WINAPI GDUPump (LPVOID arg)
{
    MSG msg;
    WNDCLASS wc;

    if (! wcInited) {                               /* register Window class */
        memset(&wc, 0, sizeof(wc));
        wc.style         = CS_NOCLOSE;
        wc.lpfnWndProc   = GDUWndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = APPCLASS;

        if (! RegisterClass(&wc)) {
            GDUPumpID = 0;
            return 0;
        }

        wcInited = TRUE;
    }

    if (hGreenPen == NULL)
        hGreenPen = CreatePen(PS_SOLID, 1, RGB_GREEN);

#ifdef DEBUG_LIGHTPEN
    if (hRedPen == NULL)
        hRedPen = CreatePen(PS_SOLID, 1, RGB_RED);
#endif

    if (hRedBrush == NULL)
        hRedBrush = CreateSolidBrush(RGB_RED);

    hGrayBrush = GetStockObject(GRAY_BRUSH);
    hDarkBrush = GetStockObject(DKGRAY_BRUSH);
    hBlackPen  = GetStockObject(BLACK_PEN);

    if (hwGDU == NULL) {                            /* create window */
        hwGDU = CreateWindow(APPCLASS,
            "2250 Display",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT,           // initial x, y position
            INITSIZE+2*INDWIDTH, INITSIZE,          // initial width and height
            NULL,                                   // parent window handle
            NULL,                                   // use class menu
            hInstance,                              // program instance handle
            NULL);                                  // create parameters

        if (hwGDU == NULL) {
            GDUPumpID = 0;
            return 0;
        }
    }

    ShowWindow(hwGDU, SW_SHOWNOACTIVATE);           /* display it */
    UpdateWindow(hwGDU);
    
    while (GetMessage(&msg, hwGDU, 0, 0)) {         /* message pump - this basically loops forevermore */
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    painting = FALSE;

    if (hwGDU != NULL) {
        DestroyWindow(hwGDU);                       /* but if a quit message got posted, clean up */
        hwGDU = NULL;
    }

    GDUPumpID = 0;
    return 0;
}

#ifdef DEBUG_LIGHTPEN
static void ShowPenHit (int x, int y)
{
    HPEN hOldPen;

    hOldPen = SelectObject(hdcGDU, hRedPen);
    DrawLine(x-10, y-10, x+10, y+10);
    DrawLine(x-10, y+10, x+10, y-10);
    SelectObject(hdcGDU, hOldPen);
}
#endif

#endif      // _WIN32 defined
#endif      // GUI_SUPPORT defined
