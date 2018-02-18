/*
 * $Id: vttest.c,v 1.15 2005/08/06 21:09:05 phil Exp $
 * VT11 test
 * Phil Budne <phil@ultimate.com>
 * September 13, 2003
 * Substantially revised by Douglas A. Gwyn, 05 Aug 2005
 *
 *      XXX -- assumes ASCII host character set
 *
 * In addition to providing some display tests, this program serves as an
 * example of how the VT11/VS60 display processor simulator can be used
 * without a PDP-11 simulator.  The vt11_cycle() function performs a single
 * "instruction cycle" of the display processor, and display_sync() forces
 * the graphics changes to appear in the window system; thus these must be
 * iterated at a fairly rapid rate to provide reasonable interaction.  This
 * implies that "host" computation must be kept minimal per iteration, or
 * else done in a separate thread.  When using multiple threads, the display
 * file should be declared with "volatile" qualification to ensure that
 * modifications are picked up by the display-processor thread.
 *
 * Part of the fun of display-file programming is figuring out ways to
 * safely modify the display without stopping the display processor, which
 * is asynchronously interpreting the display file.
 */
#undef  FRAME1STOP      /* define to pause after first frame of a section */

#ifndef TEST_DIS
#define TEST_DIS DIS_VR48
#endif

#ifndef TEST_RES
#define TEST_RES RES_HALF
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ws.h"                         /* for ws_beep() */
#include "display.h"
#include "vt11.h"
#include "vtmacs.h"

#define USEC    3                       /* simulated microseconds per cycle;
                                           making this large causes flicker! */

#define JMPA    0160000                 /* first word of DJMP_ABS */

#define SUPSCR  021                     /* SUPERSCRIPT char */
#define SUBSCR  022                     /* SUBSCRIPT char */
#define ENDSUP  023                     /* END SUPERSCRIPT char */
#define ENDSUB  024                     /* END SUBSCRIPT char */

/* The following display file (whose words might be larger than 16 bits) is
   divided into sections, each ended by a display-stop-with-interrupt
   instruction followed by an extra word.  The display-stop interrupt handler
   replaces these two words with a jump to the start of the section, causing
   an endless refresh loop.  To advance to the next section, activate the
   "tip switch" (mouse button 1); this works even if simulating a VT11. */

#define ENDSECT LSRA(ST_STOP,SI_GENERATE,LI_SAME,IT_SAME,RF_UNSYNC,MN_SAME), 0,
#define ENDFILE LSRA(ST_STOP,SI_GENERATE,LI_SAME,IT_SAME,RF_UNSYNC,MN_SAME), 1,

/* FILE VT.  Static displays that work for both VT11 and VS60. */

unsigned short VT[] = {
    /* SECTION 1.  Box just inside VR14 area using all four line types.
        Suitable for VT11 and VS60. */

    LSRA(ST_SAME, SI_SAME, LI_INTENSIFY, IT_NORMAL, RF_UNSYNC, MN_SAME),

    SGM(GM_APOINT, IN_5, LP_ENA, BL_OFF, LT_SAME),
    APOINT(I_OFF, 0, 0),

    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_LDASH),
    LVECT(I_ON, 01777, 0),

    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SDASH),
    LVECT(I_ON, 0, 01377),

    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_DDASH),
    LVECT(I_ON, -01777, 0),

    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SOLID),
    LVECT(I_ON, 0, -01377),

    ENDSECT

    /* SECTION 2.  All text characters (both normal and italic).
        Suitable for VT11 and VS60. */

    LSRA(ST_SAME, SI_SAME, LI_INTENSIFY, IT_NORMAL, RF_UNSYNC, MN_SAME),

    /* normal text */
    SGM(GM_APOINT, IN_7, LP_ENA, BL_OFF, LT_SAME),
    APOINT(I_OFF, 0, 736),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SOLID),
    CHAR(' ',' '), CHAR('A','B'), CHAR('C','D'), CHAR('E','F'), CHAR('G','H'),
    CHAR('I','J'), CHAR('K','L'), CHAR('M','N'), CHAR('O','P'), CHAR('Q','R'),
    CHAR('S','T'), CHAR('U','V'), CHAR('W','X'), CHAR('Y','Z'), CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('a','b'), CHAR('c','d'), CHAR('e','f'), CHAR('g','h'),
    CHAR('i','j'), CHAR('k','l'), CHAR('m','n'), CHAR('o','p'), CHAR('q','r'),
    CHAR('s','t'), CHAR('u','v'), CHAR('w','x'), CHAR('y','z'), CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('0','1'), CHAR('2','3'), CHAR('4','5'), CHAR('6','7'),
    CHAR('8','9'), CHAR(' ','!'), CHAR('"','#'), CHAR('$','%'), CHAR('&','\''),
    CHAR('(',')'), CHAR('*','+'), CHAR(',','-'), CHAR('.','/'), CHAR('@',0),
    CHAR('\r','\n'),
    CHAR(' ',' '), CHAR(':',';'), CHAR('<','='), CHAR('>','?'), CHAR('[','\\'),
    CHAR(']','^'), CHAR('_','`'), CHAR('{','|'), CHAR('}','~'), CHAR(127,0),
    CHAR('\r','\n'),
    CHAR(' ',' '), CHAR(14,0), CHAR(1,2), CHAR(3,4), CHAR(5,6), CHAR(7,8),
    CHAR(9,10), CHAR(11,12), CHAR(13,14), CHAR(16,17), CHAR(18,19), CHAR(20,21),
    CHAR(22,23), CHAR(24,25), CHAR(26,27), CHAR(28,29), CHAR(30,31),
    CHAR(15,0), CHAR('\r','\n'),

    /* italic text */
    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_ITALIC, RF_UNSYNC, MN_SAME),

    /* note no SGMhere */
    CHAR(' ',' '), CHAR('A','B'), CHAR('C','D'), CHAR('E','F'), CHAR('G','H'),
    CHAR('I','J'), CHAR('K','L'), CHAR('M','N'), CHAR('O','P'), CHAR('Q','R'),
    CHAR('S','T'), CHAR('U','V'), CHAR('W','X'), CHAR('Y','Z'), CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('a','b'), CHAR('c','d'), CHAR('e','f'), CHAR('g','h'),
    CHAR('i','j'), CHAR('k','l'), CHAR('m','n'), CHAR('o','p'), CHAR('q','r'),
    CHAR('s','t'), CHAR('u','v'), CHAR('w','x'), CHAR('y','z'), CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('0','1'), CHAR('2','3'), CHAR('4','5'), CHAR('6','7'),
    CHAR('8','9'), CHAR(' ','!'), CHAR('"','#'), CHAR('$','%'), CHAR('&','\''),
    CHAR('(',')'), CHAR('*','+'), CHAR(',','-'), CHAR('.','/'), CHAR('@',0),
    CHAR('\r','\n'),
    CHAR(' ',' '), CHAR(':',';'), CHAR('<','='), CHAR('>','?'), CHAR('[','\\'),
    CHAR(']','^'), CHAR('_','`'), CHAR('{','|'), CHAR('}','~'), CHAR(127,0),
    CHAR('\r','\n'),
    CHAR(' ',' '), CHAR(14,0), CHAR(1,2), CHAR(3,4), CHAR(5,6), CHAR(7,8),
    CHAR(9,10), CHAR(11,12), CHAR(13,14), CHAR(16,17), CHAR(18,19), CHAR(20,21),
    CHAR(22,23), CHAR(24,25), CHAR(26,27), CHAR(28,29), CHAR(30,31),
    CHAR(15,0), CHAR('\r','\n'),

    ENDSECT

    /* SECTION 3.  Fancy display involving all VT11 graphic modes.
        Suitable for VT11 and VS60. */

    LSRA(ST_SAME, SI_SAME, LI_INTENSIFY, IT_NORMAL, RF_UNSYNC, MN_SAME),

    /* normal text */
    SGM(GM_APOINT, IN_4, LP_ENA, BL_OFF, LT_SAME),
    APOINT(I_OFF, 0, 01340),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SOLID),
    CHAR(' ',' '), CHAR('A','B'), CHAR('C','D'), CHAR('E','F'), CHAR('G','H'),
    CHAR('I','J'), CHAR('K','L'), CHAR('M','N'), CHAR('O','P'), CHAR('Q','R'),
    CHAR('S','T'), CHAR('U','V'), CHAR('W','X'), CHAR('Y','Z'), CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('a','b'), CHAR('c','d'), CHAR('e','f'), CHAR('g','h'),
    CHAR('i','j'), CHAR('k','l'), CHAR('m','n'), CHAR('o','p'), CHAR('q','r'),
    CHAR('s','t'), CHAR('u','v'), CHAR('w','x'), CHAR('y','z'), CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('0','1'), CHAR('2','3'), CHAR('4','5'), CHAR('6','7'),
    CHAR('8','9'), CHAR(' ','!'), CHAR('"','#'), CHAR('$','%'), CHAR('&','\''),
    CHAR('(',')'), CHAR('*','+'), CHAR(',','-'), CHAR('.','/'), CHAR('@',0),
    CHAR('\r','\n'),
    CHAR(' ',' '), CHAR(':',';'), CHAR('<','='), CHAR('>','?'), CHAR('[','\\'),
    CHAR(']','^'), CHAR('_','`'), CHAR('{','|'), CHAR('}','~'), CHAR(127,0),
    CHAR('\r','\n'),
    CHAR(' ',' '), CHAR(14,0), CHAR(1,2), CHAR(3,4), CHAR(5,6), CHAR(7,8),
    CHAR(9,10), CHAR(11,12), CHAR(13,14), CHAR(16,17), CHAR(18,19), CHAR(20,21),
    CHAR(22,23), CHAR(24,25), CHAR(26,27), CHAR(28,29), CHAR(30,31),
    CHAR(15,0), CHAR('\r','\n'),

    /* italic text */
    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_ITALIC, RF_UNSYNC, MN_SAME),

    /* note no SGMhere */
    CHAR(' ',' '), CHAR('A','B'), CHAR('C','D'), CHAR('E','F'), CHAR('G','H'),
    CHAR('I','J'), CHAR('K','L'), CHAR('M','N'), CHAR('O','P'), CHAR('Q','R'),
    CHAR('S','T'), CHAR('U','V'), CHAR('W','X'), CHAR('Y','Z'), CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('a','b'), CHAR('c','d'), CHAR('e','f'), CHAR('g','h'),
    CHAR('i','j'), CHAR('k','l'), CHAR('m','n'), CHAR('o','p'), CHAR('q','r'),
    CHAR('s','t'), CHAR('u','v'), CHAR('w','x'), CHAR('y','z'), CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('0','1'), CHAR('2','3'), CHAR('4','5'), CHAR('6','7'),
    CHAR('8','9'), CHAR(' ','!'), CHAR('"','#'), CHAR('$','%'), CHAR('&','\''),
    CHAR('(',')'), CHAR('*','+'), CHAR(',','-'), CHAR('.','/'), CHAR('@',0),
    CHAR('\r','\n'),
    CHAR(' ',' '), CHAR(':',';'), CHAR('<','='), CHAR('>','?'), CHAR('[','\\'),
    CHAR(']','^'), CHAR('_','`'), CHAR('{','|'), CHAR('}','~'), CHAR(127,0),
    CHAR('\r','\n'),
    CHAR(' ',' '), CHAR(14,0), CHAR(1,2), CHAR(3,4), CHAR(5,6), CHAR(7,8),
    CHAR(9,10), CHAR(11,12), CHAR(13,14), CHAR(16,17), CHAR(18,19), CHAR(20,21),
    CHAR(22,23), CHAR(24,25), CHAR(26,27), CHAR(28,29), CHAR(30,31),
    CHAR(15,0), CHAR('\r','\n'),

    /* labeled lines of all types, blinks, and intensities (LP intr disabled) */
    LSRA(ST_SAME, SI_SAME, LI_INTENSIFY, IT_NORMAL, RF_UNSYNC, MN_SAME),

    SGM(GM_APOINT, IN_SAME, LP_DIS, BL_SAME, LT_SAME),
    APOINT(I_OFF, 020, 0740),

    SGM(GM_CHAR, IN_0, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','N'), CHAR('T',' '), CHAR('0',0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_ON, 0140, 0740),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    APOINT(I_ON, 0150, 0740),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0160, 0740),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SOLID),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_LDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_DDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 020, 0700),

    SGM(GM_CHAR, IN_1, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','N'), CHAR('T',' '), CHAR('1',0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_ON, 0140, 0700),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    APOINT(I_ON, 0150, 0700),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0160, 0700),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SOLID),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_LDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_DDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 020, 0640),

    SGM(GM_CHAR, IN_2, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','N'), CHAR('T',' '), CHAR('2',0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_ON, 0140, 0640),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    APOINT(I_ON, 0150, 0640),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0160, 0640),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SOLID),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_LDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_DDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 020, 0600),

    SGM(GM_CHAR, IN_3, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','N'), CHAR('T',' '), CHAR('3',0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_ON, 0140, 0600),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    APOINT(I_ON, 0150, 0600),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0160, 0600),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SOLID),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_LDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_DDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 020, 0540),

    SGM(GM_CHAR, IN_4, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','N'), CHAR('T',' '), CHAR('4',0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_ON, 0140, 0540),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    APOINT(I_ON, 0150, 0540),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0160, 0540),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SOLID),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_LDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_DDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 020, 0500),

    SGM(GM_CHAR, IN_5, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','N'), CHAR('T',' '), CHAR('5',0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_ON, 0140, 0500),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    APOINT(I_ON, 0150, 0500),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0160, 0500),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SOLID),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_LDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_DDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 020, 0440),

    SGM(GM_CHAR, IN_6, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','N'), CHAR('T',' '), CHAR('6',0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_ON, 0140, 0440),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    APOINT(I_ON, 0150, 0440),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0160, 0440),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SOLID),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_LDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_DDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 020, 0400),

    SGM(GM_CHAR, IN_7, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','N'), CHAR('T',' '), CHAR('7',0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_ON, 0140, 0400),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    APOINT(I_ON, 0150, 0400),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0160, 0400),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SOLID),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_LDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_DDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    /* similar, but LP intr enabled, official threshold intensities */
    SGM(GM_APOINT, IN_SAME, LP_ENA, BL_SAME, LT_SAME),
    APOINT(I_OFF, 020, 0340),

    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_ITALIC, RF_UNSYNC, MN_SAME),

    SGM(GM_CHAR, IN_6, LP_SAME, BL_ON, LT_SAME),
    CHAR('I','N'), CHAR('T','R'),

    SGM(GM_APOINT, IN_4, LP_SAME, BL_OFF, LT_SAME),
    APOINT(I_ON, 0140, 0340),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    APOINT(I_ON, 0150, 0340),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0160, 0340),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SOLID),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_LDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_SDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_OFF, LT_DDASH),
    SVECT(I_ON, 060, 0),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    SVECT(I_ON, 060, 0),

    /* graphplots */
    SGM(GM_APOINT, IN_5, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_ON, 040, 0200),

    LSRB(CL_SAME, SS_CHANGE, 040),

    SGM(GM_GRAPHY, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    GRAPHY(I_ON, 0160),
    GRAPHY(I_ON, 0140),
    GRAPHY(I_ON, 0120),
    GRAPHY(I_ON, 0100),
    GRAPHY(I_ON, 0060),
    GRAPHY(I_ON, 0040),

    SGM(GM_RPOINT, IN_SAME, LP_SAME, BL_OFF, LT_SAME),
    RPOINT(I_OFF, 0040, 0),
    RPOINT(I_ON, 0040, 0),

    LSRB(CL_SAME, SS_CHANGE, 020),

    SGM(GM_GRAPHX, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    GRAPHX(I_ON, 0500),
    GRAPHX(I_ON, 0540),
    GRAPHX(I_ON, 0600),
    GRAPHX(I_ON, 0640),
    GRAPHX(I_ON, 0700),
    GRAPHX(I_ON, 0740),

    /* long vectors in all directions from a common origin */
    SGM(GM_APOINT, IN_4, LP_SAME, BL_SAME, LT_SOLID),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0400, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0400, 0100),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0400, 0200),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0400, 0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0300, 0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0200, 0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0100, 0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0, 0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0100, 0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0200, 0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0300, 0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0400, 0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0400, 0200),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0400, 0100),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0400, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0400, -0100),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0400, -0200),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0400, -0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0300, -0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0200, -0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, -0100, -0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0, -0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0100, -0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0200, -0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0300, -0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0400, -0300),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0400, -0200),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01400, 01100),
    SGM(GM_LVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LVECT(I_ON, 0400, -0100),

    /* nearby lines with varied spacing */
    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01200, 0500),

    SGM(GM_SVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    SVECT(I_ON, 077, 0),
    SVECT(I_OFF, -077, -1),
    SVECT(I_ON, 077, 0),
    SVECT(I_OFF, -077, -2),
    SVECT(I_ON, 077, 0),
    SVECT(I_OFF, -077, -3),
    SVECT(I_ON, 077, 0),
    SVECT(I_OFF, -077, -4),
    SVECT(I_ON, 077, 0),
    SVECT(I_OFF, -077, -5),
    SVECT(I_ON, 077, 0),
    SVECT(I_OFF, -077, -6),
    SVECT(I_ON, 077, 0),
    SVECT(I_OFF, -077, -7),
    SVECT(I_ON, 077, 0),
    SVECT(I_OFF, -077, -010),
    SVECT(I_ON, 077, 0),
    SVECT(I_OFF, -077, 044),
    SVECT(I_ON, 0, -077),
    SVECT(I_OFF, 1, 077),
    SVECT(I_ON, 0, -077),
    SVECT(I_OFF, 2, 077),
    SVECT(I_ON, 0, -077),
    SVECT(I_OFF, 3, 077),
    SVECT(I_ON, 0, -077),
    SVECT(I_OFF, 4, 077),
    SVECT(I_ON, 0, -077),
    SVECT(I_OFF, 5, 077),
    SVECT(I_ON, 0, -077),
    SVECT(I_OFF, 6, 077),
    SVECT(I_ON, 0, -077),
    SVECT(I_OFF, 7, 077),
    SVECT(I_ON, 0, -077),
    SVECT(I_OFF, 010, 077),
    SVECT(I_ON, 0, -077),

    /* all four flavors of characters (lp intr enabled, but intensity 4) */

    SGM(GM_APOINT, IN_4, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01040, 0240),

    LSRA(ST_SAME, SI_SAME, LP_SAME, IT_NORMAL, RF_UNSYNC, MN_SAME),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_OFF, LT_SAME),
    CHAR('N','o'), CHAR('r','m'), CHAR('a','l'),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    CHAR(' ','B'), CHAR('l','i'), CHAR('n','k'),

    SGM(GM_APOINT, IN_4, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01040, 0200),

    LSRA(ST_SAME, SI_SAME, LP_SAME, IT_ITALIC, RF_UNSYNC, MN_SAME),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','t'), CHAR('a','l'), CHAR('i','c'),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    CHAR(' ','B'), CHAR('l','i'), CHAR('n','k'),

    /* all eight intensities of characters (lp intr enabled) */

    LSRA(ST_SAME, SI_SAME, LP_SAME, IT_NORMAL, RF_UNSYNC, MN_SAME),

    SGM(GM_APOINT, IN_5, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01040, 0100),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_OFF, LT_SAME),
    CHAR('I','N'), CHAR('T',' '),

    SGM(GM_CHAR, IN_0, LP_SAME, BL_SAME, LT_SAME),
    CHAR('0',0),
    SGM(GM_CHAR, IN_1, LP_SAME, BL_SAME, LT_SAME),
    CHAR('1',0),
    SGM(GM_CHAR, IN_2, LP_SAME, BL_SAME, LT_SAME),
    CHAR('2',0),
    SGM(GM_CHAR, IN_3, LP_SAME, BL_SAME, LT_SAME),
    CHAR('3',0),
    SGM(GM_CHAR, IN_4, LP_SAME, BL_SAME, LT_SAME),
    CHAR('4',0),
    SGM(GM_CHAR, IN_5, LP_SAME, BL_SAME, LT_SAME),
    CHAR('5',0),
    SGM(GM_CHAR, IN_6, LP_SAME, BL_SAME, LT_SAME),
    CHAR('6',0),
    SGM(GM_CHAR, IN_7, LP_SAME, BL_SAME, LT_SAME),
    CHAR('7',0),

    /* XXX -- more can be included in this pattern */

    ENDSECT

    /* SECTION 4.  Clipping tests.
        Suitable for VT11 and VS60. */

    LSRA(ST_SAME, SI_SAME, LI_INTENSIFY, IT_NORMAL, RF_UNSYNC, MN_SAME),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01000, 01000),

    SGM(GM_LVECT, IN_4, LP_ENA, BL_OFF, LT_SOLID),
    LVECT(I_ON, 01100, 0),
    LVECT(I_ON, -01100, 01100),
    LVECT(I_ON, 0, -01100),
    LVECT(I_OFF, 0, 01100),
    LVECT(I_ON, -01100, -01100),
    LVECT(I_ON, 01100, 0),
    LVECT(I_ON, 0, -01100),
    LVECT(I_ON, -01100, 01100),
    LVECT(I_OFF, 01100, 0),
    LVECT(I_OFF, 01100, 0),
    LVECT(I_ON, -01100, -01100),

    ENDSECT

    /* END OF TEST SECTIONS. */

    ENDFILE
};

/* FILE LP.  Dynamic light pen tracking; works for both VT11 and VS60. */

unsigned short LP[] = {
    /* SECTION 1.  "rubber-band" dot-dash vector to tracking object. */

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01000, 01000),        /* screen center */

    SGM(GM_LVECT, IN_4, LP_DIS, BL_SAME, LT_DDASH),
    /* following coordinates are updated by LP hit intr. handler: */
    LVECT(I_ON, 0, 0),                  /* tracking object center */

    SGM(GM_SVECT, IN_7, LP_ENA, BL_SAME, LT_SOLID),
    SVECT(I_OFF, 0, 30),
    SVECT(I_ON, 0, -60),
    SVECT(I_OFF, 30, 30),
    SVECT(I_ON, -60, 0),
    SVECT(I_ON, 30, 30),
    SVECT(I_ON, 30, -30),
    SVECT(I_ON, -30, -30),
    SVECT(I_ON, -30, 30),
    SVECT(I_OFF, 10, 0),
    SVECT(I_ON, 20, 20),
    SVECT(I_ON, 20, -20),
    SVECT(I_ON, -20, -20),
    SVECT(I_ON, -20, 20),
    SVECT(I_OFF, 10, 0),
    SVECT(I_ON, 10, 10),
    SVECT(I_ON, 10, -10),
    SVECT(I_ON, -10, -10),
    SVECT(I_ON, -10, 10),
#if 0                                   /* not needed for this app */
    SVECT(I_OFF, 0, -10),               /* "flyback" vector */
#endif

    ENDSECT

    /* END OF TEST SECTIONS. */

    ENDFILE
};

/* FILE VS.  Static displays that work only for VS60. */

unsigned short VS[] = {
    /* SECTION 0.  Warning that VS60 is required. */

    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_NORMAL, RF_UNSYNC, MN_SAME),

    SGM(GM_APOINT, IN_7, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0300, 01000),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_OFF, LT_SAME),
    CHAR('F','o'), CHAR('l','l'), CHAR('o','w'), CHAR('i','n'), CHAR('g',' '),
        CHAR('t','e'), CHAR('s','t'), CHAR('s',' '), CHAR('d','o'),
        CHAR(' ','n'), CHAR('o','t'), CHAR(' ','w'), CHAR('o','r'),
        CHAR('k',' '), CHAR('f','o'), CHAR('r',' '), CHAR('V','T'),
        CHAR('1','1'), CHAR(';',0),

    /* italic text */
    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_ITALIC, RF_UNSYNC, MN_SAME),

    SGM(GM_APOINT, IN_7, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0340, 00720),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_ON, LT_SAME),
    CHAR('S','T'), CHAR('O','P'), CHAR(' ','P'), CHAR('R','O'), CHAR('G','R'),
    CHAR('A','M'), CHAR(' ','i'), CHAR('f',' '), CHAR('n','o'), CHAR('t',' '),
    CHAR('u','s'), CHAR('i','n'), CHAR('g',' '), CHAR('V','R'), CHAR('4','8'),
    CHAR('!',0),

    ENDSECT

    /* SECTION 1.  Variety of text characters. */

    LSRA(ST_SAME, SI_SAME, LI_BRIGHTDOWN, IT_SAME, RF_UNSYNC, MN_MAIN),

    /* horizontal text, 4 sizes */
    SGM(GM_APOINT, IN_4, LP_ENA, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0, 01600),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LSRC(RO_SAME, CS_CHANGE, 0, VS_SAME, 0),
    CHAR(' ',' '), CHAR('S','m'), CHAR('a','l'), CHAR('l',':'), CHAR(' ','1'),
        CHAR('/','2'),
    LSRC(RO_SAME, CS_CHANGE, 1, VS_SAME, 0),
    CHAR(' ',' '), CHAR('N','o'), CHAR('r','m'), CHAR('a','l'), CHAR(':',' '),
        CHAR('1',0),
    LSRC(RO_SAME, CS_CHANGE, 2, VS_SAME, 0),
    CHAR(' ',' '), CHAR('B','i'), CHAR('g',':'), CHAR(' ','1'), CHAR('-','1'),
        CHAR('/','2'),
    LSRC(RO_SAME, CS_CHANGE, 3, VS_SAME, 0),
    CHAR(' ',' '), CHAR('L','a'), CHAR('r','g'), CHAR('e',':'), CHAR(' ','2'),
        CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('A',SUBSCR), CHAR('B',SUBSCR), CHAR('C',SUBSCR),
        CHAR('D',ENDSUB), CHAR(ENDSUB,ENDSUB), CHAR('W',SUPSCR),
        CHAR('X',SUPSCR), CHAR('Y',SUPSCR), CHAR('Z',ENDSUP),
        CHAR(ENDSUP,ENDSUP), CHAR('!','!'),

    /* vertical text, 4 sizes */
    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0200, 0),

    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    LSRC(RO_VERTICAL, CS_CHANGE, 0, VS_SAME, 0),
    CHAR(' ',' '), CHAR('S','m'), CHAR('a','l'), CHAR('l',':'), CHAR(' ','1'),
        CHAR('/','2'),
    LSRC(RO_SAME, CS_CHANGE, 1, VS_SAME, 0),
    CHAR(' ',' '), CHAR('N','o'), CHAR('r','m'), CHAR('a','l'), CHAR(':',' '),
        CHAR('1',0),
    LSRC(RO_SAME, CS_CHANGE, 2, VS_SAME, 0),
    CHAR(' ',' '), CHAR('B','i'), CHAR('g',':'), CHAR(' ','1'), CHAR('-','1'),
        CHAR('/','2'),
    LSRC(RO_SAME, CS_CHANGE, 3, VS_SAME, 0),
    CHAR(' ',' '), CHAR('L','a'), CHAR('r','g'), CHAR('e',':'), CHAR(' ','2'),
        CHAR('\r','\n'),
    CHAR(' ',' '), CHAR('A',SUBSCR), CHAR('B',SUBSCR), CHAR('C',SUBSCR),
        CHAR('D',ENDSUB), CHAR(ENDSUB,ENDSUB), CHAR('W',SUPSCR),
        CHAR('X',SUPSCR), CHAR('Y',SUPSCR), CHAR('Z',ENDSUP),
        CHAR(ENDSUP,ENDSUP), CHAR('!','!'),

    /* horizontal text, sub/superscript examples from DECgraphic-11 manual */
    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0400, 01200),

    LSRC(RO_HORIZONTAL, CS_CHANGE, 2, VS_SAME, 0),
    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    CHAR('C',SUBSCR), CHAR('2',ENDSUB), CHAR('H',SUBSCR), CHAR('5',ENDSUB),
        CHAR('O','H'), CHAR(' ',' '),
    CHAR(016,000), CHAR(017,'='), CHAR(016,003), CHAR(017,'('),
        CHAR('x',SUBSCR), CHAR('i',ENDSUB), CHAR('-','q'), CHAR(SUBSCR,'i'),
        CHAR(ENDSUB,')'), CHAR(SUPSCR,'2'), CHAR(ENDSUP,'e'), CHAR(SUPSCR,'-'),
        CHAR('i',SUPSCR), CHAR('2',ENDSUP), CHAR(ENDSUP,0),

    LSRC(RO_SAME, CS_CHANGE, 1, VS_SAME, 0),
    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_SAME, RF_SAME, MN_MENU),
    SGM(GM_APOINT, IN_7, LP_ENA, BL_SAME, LT_SAME),
    APOINT(I_OFF, 0, 1000),
    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    CHAR('U','n'), CHAR('s','y'), CHAR('n','c'),

    ENDSECT

    /* SECTION 2.  Basic vectors (long and short). */

    LSRA(ST_SAME, SI_SAME, LI_BRIGHTDOWN, IT_SAME, RF_40, MN_MAIN),

    SGM(GM_APOINT, IN_4, LP_ENA, BL_OFF, LT_SDASH),
    APOINT(I_OFF, 01000, 01000),

    SGM(GM_GRAPHX, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    BLVECT(I_OFF, 2, 0600),
    BLVECT(I_ON, 0, 0200),
    BLVECT(I_ON, 7, 0400),
    BLVECT(I_ON, 6, 0400),
    BLVECT(I_ON, 5, 0400),
    BLVECT(I_ON, 4, 0400),
    SGM(GM_GRAPHY, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    BLVECT(I_ON, 3, 0400),
    BLVECT(I_ON, 2, 0400),
    BLVECT(I_ON, 1, 0400),
    BLVECT(I_ON, 0, 0200),
    BLVECT(I_OFF, 6, 0600),

    SGM(GM_BSVECT, IN_SAME, LP_SAME, BL_ON, LT_SOLID),
    BSVECT(I_OFF, 2, 007, 2, 016),
    BSVECT(I_ON, 0, 007, 7, 016),
    BSVECT(I_ON, 6, 016, 5, 016),
    BSVECT(I_ON, 4, 016, 3, 016),
    BSVECT(I_ON, 2, 016, 1, 016),
    BSVECT(I_ON, 0, 007, 0, 000),

    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_SAME, RF_SAME, MN_MENU),
    SGM(GM_APOINT, IN_7, LP_ENA, BL_OFF, LT_SAME),
    APOINT(I_OFF, 0, 1000),
    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    CHAR('4','0'), CHAR('H','z'), CHAR(' ','S'), CHAR('y','n'), CHAR('c',0),

    ENDSECT

    /* SECTION 3.  3D data, but depth cueing disabled. */

    LSRBB(ZD_YES, ED_ENA, DQ_OFF, ES_YES),      /* but term char not used */
    LSRA(ST_SAME, SI_SAME, LI_BRIGHTDOWN, IT_SAME, RF_30, MN_MAIN),

    SGM(GM_APOINT, IN_4, LP_ENA, BL_OFF, LT_LDASH),
    APOINT3(I_OFF, 0200, 0200, 0400),

    SGM(GM_AVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    AVECT3(I_ON, 01200, 00200, 0400),
    AVECT3(I_ON, 01200, 01200, 0400),
    AVECT3(I_ON, 00200, 01200, 0400),
    AVECT3(I_ON, 00200, 00200, 0400),
    AVECT3(I_OFF, 00600, 00600, -0400),
    AVECT3(I_ON, 01600, 00600, -0400),
    AVECT3(I_ON, 01600, 01600, -0400),
    AVECT3(I_ON, 00600, 01600, -0400),
    AVECT3(I_ON, 00600, 00600, -0400),
    SGM(GM_AVECT, IN_SAME, LP_SAME, BL_SAME, LT_SOLID),
    AVECT3(I_ON, 00200, 00200, 0400),
    AVECT3(I_OFF, 01200, 00200, 0400),
    AVECT3(I_ON, 01600, 00600, -0400),
    AVECT3(I_OFF, 01600, 01600, -0400),
    AVECT3(I_ON, 01200, 01200, 0400),
    AVECT3(I_OFF, 00200, 01200, 0400),
    AVECT3(I_ON, 00600, 01600, -0400),

    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_SAME, RF_SAME, MN_MENU),
    SGM(GM_APOINT, IN_7, LP_ENA, BL_OFF, LT_SAME),
    APOINT3(I_OFF, 0, 1000, 0200),
    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    CHAR('3','0'), CHAR('H','z'), CHAR(' ','S'), CHAR('y','n'), CHAR('c',0),

    ENDSECT

    /* SECTION 4. 3D data, with depth cueing enabled. */

    LSRBB(ZD_YES, ED_ENA, DQ_ON, ES_YES),       /* but term char not used */
    LSRA(ST_SAME, SI_SAME, LI_BRIGHTDOWN, IT_SAME, RF_EXT, MN_MAIN),

    SGM(GM_APOINT, IN_4, LP_ENA, BL_OFF, LT_DDASH),
    APOINT3(I_OFF, 0200, 0200, 0400),

    SGM(GM_AVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    AVECT3(I_ON, 01200, 00200, 0400),
    AVECT3(I_ON, 01200, 01200, 0400),
    AVECT3(I_ON, 00200, 01200, 0400),
    AVECT3(I_ON, 00200, 00200, 0400),
    AVECT3(I_OFF, 00600, 00600, -0400),
    AVECT3(I_ON, 01600, 00600, -0400),
    AVECT3(I_ON, 01600, 01600, -0400),
    AVECT3(I_ON, 00600, 01600, -0400),
    AVECT3(I_ON, 00600, 00600, -0400),
    SGM(GM_AVECT, IN_SAME, LP_SAME, BL_SAME, LT_SOLID),
    AVECT3(I_ON, 00200, 00200, 0400),
    AVECT3(I_OFF, 01200, 00200, 0400),
    AVECT3(I_ON, 01600, 00600, -0400),
    AVECT3(I_OFF, 01600, 01600, -0400),
    AVECT3(I_ON, 01200, 01200, 0400),
    AVECT3(I_OFF, 00200, 01200, 0400),
    AVECT3(I_ON, 00600, 01600, -0400),

    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_SAME, RF_SAME, MN_MENU),
    SGM(GM_APOINT, IN_7, LP_ENA, BL_OFF, LT_SAME),
    APOINT3(I_OFF, 0, 1000, 0200),
    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    CHAR('E','x'), CHAR('t','.'), CHAR(' ','S'), CHAR('y','n'), CHAR('c',0),

    ENDSECT

    /* SECTION 5. Circles and arcs. */

    SGM(GM_APOINT, IN_4, LP_ENA, BL_ON, LT_SOLID),
    APOINT(I_OFF, 0500, 01400),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    ARC(I_ON, -0100, 0, 0, 0),

    SGM(GM_APOINT, IN_5, LP_SAME, BL_OFF, LT_SDASH),
    APOINT(I_OFF, 0532, 01532),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    ARC(I_ON, -0132, -0132, 0, -0264),

    SGM(GM_APOINT, IN_6, LP_SAME, BL_SAME, LT_LDASH),
    APOINT(I_OFF, 0400, 01700),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    ARC(I_ON, 0, -0300, 0, -0600),

    SGM(GM_APOINT, IN_7, LP_SAME, BL_SAME, LT_DDASH),
    APOINT(I_OFF, 0114, 01664),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    ARC(I_ON, 0264, -0264, 0, -0550),

    SGM(GM_APOINT, IN_4, LP_SAME, BL_SAME, LT_SOLID),
    APOINT(I_OFF, 01400, 01400),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    ARC(I_ON, 0, 0, 0400, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SDASH),
    APOINT(I_OFF, 0500, 0400),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    ARC(I_ON, -0100, 0, 0200, 0),

    SGM(GM_APOINT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    APOINT(I_OFF, 01600, 0400),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_SOLID),
    ARC(I_ON, -0200, 0, -0200, 0300),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_SDASH),
    ARC(I_ON, 0, -0300, -0200, -0300),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_LDASH),
    ARC(I_ON, 0200, 0, 0200, -0300),
    SGM(GM_ARC, IN_SAME, LP_SAME, BL_SAME, LT_DDASH),
    ARC(I_ON, 0, 0300, 0200, 0300),

    ENDSECT

    /* SECTION 6. Display subroutines, with and without parameter restore. */

    /* XXX  need test for subroutines */

    /* SECTION 7. Offset, vector scale, and clipping. */

    LSRA(ST_SAME, SI_SAME, LI_BRIGHTDOWN, IT_NORMAL, RF_UNSYNC, MN_MAIN),
    LSRC(RO_HORIZONTAL, CS_CHANGE, 1, VS_CHANGE, 4),

    SGM(GM_APOINT, IN_3, LP_ENA, BL_OFF, LT_SOLID),
    OFFSET(0, 0),
    APOINT(I_ON, 01040, 01040),
    APOINT(I_ON, 01040, 0740),
    APOINT(I_ON, 0740, 01040),
    APOINT(I_ON, 0740, 0740),

    SGM(GM_APOINT, IN_5, LP_SAME, BL_ON, LT_SAME),
    OFFSET(06, 010),
    APOINT(I_ON, 01040, 01040),
    APOINT(I_ON, 01040, 0740),
    APOINT(I_ON, 0740, 01040),
    APOINT(I_ON, 0740, 0740),

    OFFSET(014, 020),
    LSRC(RO_HORIZONTAL, CS_SAME, 0, VS_CHANGE, 8),
    SGM(GM_APOINT, IN_7, LP_ENA, BL_SAME, LT_SAME),
    APOINT(I_ON, 0420, 0420),
    SGM(GM_RPOINT, IN_7, LP_SAME, BL_SAME, LT_SAME),
    RPOINT(I_ON, 0, -040),
    RPOINT(I_ON, -040, 040),
    RPOINT(I_ON, 0, -040),

    /* XXX  need test for clipping */

    ENDSECT

    /* END OF TEST SECTIONS. */

    ENDFILE
};

/* FILE WF.  Rotating wire-frame display that works only for VS60. */

unsigned short WF[] = {

    /* SECTION 1. 3D data, with depth cueing enabled. */

    LSRBB(ZD_YES, ED_ENA, DQ_ON, ES_NO),
    LSRA(ST_SAME, SI_SAME, LI_BRIGHTDOWN, IT_SAME, RF_40, MN_MAIN),

    SGM(GM_APOINT, IN_4, LP_ENA, BL_OFF, LT_DDASH),
    APOINT3(I_OFF, 0, 0, 0),    /* cube coords filled in by wf_update() */

    SGM(GM_AVECT, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_OFF, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),
    SGM(GM_AVECT, IN_SAME, LP_SAME, BL_SAME, LT_SOLID),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_OFF, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_OFF, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),
    AVECT3(I_OFF, 0, 0, 0),
    AVECT3(I_ON, 0, 0, 0),

    LSRA(ST_SAME, SI_SAME, LI_SAME, IT_SAME, RF_SAME, MN_MENU),
    SGM(GM_APOINT, IN_7, LP_ENA, BL_OFF, LT_SAME),
    APOINT3(I_OFF, 0, 1000, 0200),
    SGM(GM_CHAR, IN_SAME, LP_SAME, BL_SAME, LT_SAME),
    CHAR('4','0'), CHAR('H','z'), CHAR(' ','S'), CHAR('y','n'), CHAR('c',0),

    ENDSECT

    /* END OF TEST SECTIONS. */

    ENDFILE
};

static unsigned short *df;              /* -> start of current display file */
static uint16 start;                    /* initial DPC for section of d.file */
static int more;                        /* set until end of d.file seen */

static void
wf_update(int first_time) {
    double c, s;                        /* cosine, sine of rotation angle */
    int x, y, z;                        /* rotated coordinates */
    static int xc = 01000, yc = 01000, zc = 0;  /* cube center coords */
    static int vp = 010000;             /* distance to vanishing point */
    static struct {
        int offset;                     /* WF[] offset (words) of vector data */
        int i;                          /* I_ON or I_OFF */
        int x, y, z;                    /* coords of cube corner point */
    } *dp, data[] = {
        3, I_OFF, 00400, 00400, 0400,
        7, I_ON, 01400, 00400, 0400,
        10, I_ON, 01400, 01400, 0400,
        13, I_ON, 00400, 01400, 0400,
        16, I_ON, 00400, 00400, 0400,
        19, I_OFF, 00400, 00400, -0400,
        22, I_ON, 01400, 00400, -0400,
        25, I_ON, 01400, 01400, -0400,
        28, I_ON, 00400, 01400, -0400,
        31, I_ON, 00400, 00400, -0400,
        35, I_ON, 00400, 00400, 0400,
        38, I_OFF, 01400, 00400, 0400,
        41, I_ON, 01400, 00400, -0400,
        44, I_OFF, 01400, 01400, -0400,
        47, I_ON, 01400, 01400, 0400,
        50, I_OFF, 00400, 01400, 0400,
        53, I_ON, 00400, 01400, -0400,
        -1                              /* end-of-data marker */
    };
    static double rot = 0.0;            /* total amount of rotation, degrees */
    if (first_time) {
        /* tilt cube toward viewer */
        c = cos(30.0 * 3.14159/180.0);
        s = sin(30.0 * 3.14159/180.0);
        for (dp = data; dp->offset >= 0; ++dp) {
                z = zc + ((dp->z - zc) * c + (dp->y - yc) * s);
                y = yc + ((dp->y - yc) * c - (dp->z - zc) * s);
                WF[dp->offset    ] = dp->i | (SGN_(dp->x) << 13) | MAG_(dp->x);
                WF[dp->offset + 1] = (SGN_(y) << 13) | MAG_(y);
                WF[dp->offset + 2] = (SGN_(z) << 13) | (MAG_(z) << 2);
                /* X coord. unchanged because rotation is parallel to X axis */
                dp->y = y;
                dp->z = z;
        }
    } else
        if ((rot += 1.0) >= 360.0)      /* rotation increment */
            rot -= 360.0;
    c = cos(rot * 3.14159/180.0);
    s = sin(rot * 3.14159/180.0);
    for (dp = data; dp->offset >= 0; ++dp) {
        x = xc + ((dp->x - xc) * c + (dp->z - zc) * s);
        z = zc + ((dp->z - zc) * c - (dp->x - xc) * s);
        /* apply (approximate) perspective */
        x = x * (1.0 + (double)z / vp );
        y = dp->y * (1.0 + (double)z / vp );
        WF[dp->offset    ] = dp->i | (SGN_(x) << 13) | MAG_(x);
        WF[dp->offset + 1] = (SGN_(y) << 13) | MAG_(y);
        WF[dp->offset + 2] = (SGN_(z) << 13) | (MAG_(z) << 2);
    }
}

int
main(void) {
    int c;

    vt11_display = TEST_DIS;
    vt11_scale = TEST_RES;

    /* VT11/VS60 tests */

    puts("initial tests work for both VT11 and VS60");
    for (df = VT, start = 0, more = 1; more; ) {
        vt11_reset(NULL, 0);            /* reset everything */
        vt11_set_dpc(start);            /* start section */
        c = 0;
        while (vt11_cycle(USEC, 1)) {
            display_sync();             /* XXX push down? */
            if (display_lp_sw)          /* tip switch activated */
                c = 1;                  /* flag: break requested */
            if (c && !display_lp_sw)    /* wait for switch release */
                break;
        }
        /* end of section */
    }
    /* end of display file */

    /* light pen tracking */

    ws_beep();
    puts("move the light pen through the tracking object");
    fflush(stdout);
    for (df = LP, start = 0, more = 1; more; ) {
        vt11_reset(NULL, 0);            /* reset everything */
        vt11_set_dpc(start);            /* start section */
        c = 0;
        while (vt11_cycle(USEC, 1)) {
            display_sync();             /* XXX push down? */
            if (display_lp_sw)          /* tip switch activated */
                c = 1;                  /* flag: break requested */
            if (c && !display_lp_sw)    /* wait for switch release */
                break;
            /* [dynamic modifications to the display file can be done here] */
        }
        /* end of section */
    }
    /* end of display file */

    /* VS60 tests */

    ws_beep();
    puts("following tests require VS60");
    for (df = VS, start = 0, more = 1; more; ) {
        vt11_reset(NULL, 0);            /* reset everything */
        vt11_set_str((uint16)(0200 | '~'));     /* set terminating char. */
        vt11_set_anr((uint16)(040000 | (2<<12) | 04000 | 01234));
                                        /* set associative name 0123x */
        vt11_set_dpc(start);            /* start section */
        c = 0;
        while (vt11_cycle(USEC, 1)) {
            display_sync();             /* XXX push down? */
            if (display_lp_sw)          /* tip switch activated */
                c = 1;                  /* flag: break requested */
            if (c && !display_lp_sw)    /* wait for switch release */
                break;
        }
        /* end of section */
    }
    /* end of display file */

    /* VS60 rotating wire-frame display */

    puts("press and release tip switch (button 1) for next display");
    fflush(stdout);
    wf_update(1);                       /* do first-time init */
    for (df = WF, start = 0, more = 1; more; ) {
        vt11_reset(NULL, 0);            /* reset everything */
        vt11_set_dpc(start);            /* start section */
        c = 0;
        while (vt11_cycle(USEC, 1)) {
            display_sync();             /* XXX push down? */
            if (display_lp_sw)          /* tip switch activated */
                c = 1;                  /* flag: break requested */
            if (c && !display_lp_sw)    /* wait for switch release */
                break;
        }
        /* end of section */
    }
    /* end of display file */

    /* XXX  would be nice to have an example of animation  */

    return 0;
}

/*
 * callbacks from display.c
 */
void
cpu_get_switches(unsigned long *p1, unsigned long *p2) {
    *p1 = *p2 = 0;
}

void
cpu_set_switches(unsigned long w1, unsigned long w2) {
}

/*
 * callbacks from vt11.c
 */

int
vt_fetch(uint32 addr, vt11word *w) {
    *w = df[addr/2];
    return 0;
}

void
vt_stop_intr(void) {
    uint16 dpc = vt11_get_dpc();        /* -> just after DSTOP instruction */
    if (df[dpc/2] == 0) {               /* ENDSECT */
#ifdef FRAME1STOP
        int c;
        puts("end of pass through this test pattern; display frozen");
        puts("enter newline to refresh this section or EOF to quit");
        fflush(stdout);
        while ((c = getchar()) != '\n')
            if (c == EOF)
                exit(0);                /* user aborted test */
#endif
        if (df == WF) {
            wf_update(0);
            vt11_set_dpc(0);            /* restart modified display */
            start = dpc + 2;            /* save start of next section */
        } else {
            df[dpc/2 - 1] = JMPA;
            df[dpc/2] = start;
            start = dpc + 2;            /* save start of next section */
            vt11_set_dpc(dpc - 2);      /* reset; then JMPA to old start */
            puts("press and release tip switch (button 1) for next display");
            fflush(stdout);
        }
    } else                              /* ENDFILE */
        more = 0;
}

void
vt_lpen_intr(void) {
    if (df == LP) {
        int dx = (int)(vt11_get_xpr() & 01777) - 01000;
        int dy = (int)(vt11_get_ypr() & 01777) - 01000;
        if (dx < 0)
            dx = (-dx) | 020000;        /* negative */
        if (dy < 0)
            dy = (-dy) | 020000;        /* negative */

        df[4] = dx | I_ON;              /* visible */
        df[5] = dy;
    } else {
        printf("VT11 lightpen interrupt (0%o,0%o)\n",
                (unsigned)vt11_get_xpr() & 01777,
                (unsigned)vt11_get_ypr() & 01777);
        fflush(stdout);
    }
    vt11_set_dpc((uint16)1);            /* resume */
}

void
vt_char_intr(void) {
    puts("VT11 illegal character/timeout interrupt");
    fflush(stdout);
    vt11_set_dpc((uint16)1);            /* resume */
}

void
vt_name_intr(void) {
    puts("VS60 name-match interrupt");
    fflush(stdout);
    vt11_set_dpc((uint16)1);            /* resume */
}

