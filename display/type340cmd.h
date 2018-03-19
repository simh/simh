/*
 * constants for type 340 words
 * (use for writing test programs)
 */

/*
 * Copyright (c) 2018, Philip L. Budne
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
 * Except as contained in this notice, the name of the author shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from the authors.
 */


#define CHAR(C1,C2,C3) \
    (((C1)<<12)|((C2)<<6)|(C3))

#define INCRPT(P1,P2,P3,P4) (((P1)<<12)|((P2)<<8)|((P3)<<4)|(P4))

/*
 * adapted from
 * 7-13_340_Display_Programming_Manual.pdf
 * Appendix 1: Mnemonics
 *
 * tst340.c dump() routine uses/prints these
 */

/* modes: */
#define MPAR    0000000         /* parameter */
#define MPT     0020000         /* point */
#define MSLV    0040000         /* slave */
#define MCHR    0060000         /* character */
#define MVCT    0100000         /* Vector */
#define MVCTC   0120000         /* Vector continue */
#define MINCR   0140000         /* Increment */
#define MSUBR   0160000         /* Subroutine */

#define MODEMASK 0160000

/****************
 * Parameter Mode Words
 */

#define LPON    014000
#define LPOFF   010000

#define STP     0003000         /* stop & interrupt */
#define STOP    0001000         /* just stop: not in display manual */

/* scale settings */
#define S0      0000100
#define S1      0000120
#define S2      0000140
#define S3      0000160
 
/* intensity settings */
#define IN0     0000010
#define IN1     0000011
#define IN2     0000012
#define IN3     0000013
#define IN4     0000014
#define IN5     0000015
#define IN6     0000016
#define IN7     0000017
 
/****************
 * Point Mode Words
 */

#define V       0200000         /* Vertical word */
#define H       0000000         /* Horizontal word */
#define IP      0002000         /* Intensify point */

/****************
 * Slave Mode Words
 */

#define S1ON    05000
#define S1OFF   04000
#define LP1ON   02000
 
#define S2ON     0500
#define S2OFF    0400
#define LP2ON    0200
 
#define S3ON      050
#define S3OFF     040
#define LP3ON     020
 
#define S4ON       05
#define S4OFF      04
#define LP4ON      02

// for use in XXX macro:
#define SXON       05
#define SXOFF      04
#define LPXON      02

/****************
 * Character mode
 */

/* defines from ITS: 340def 4 */
#define CHRESC  037           /* escape from character mode */
#define CHRUC   035           /* shift to upper-case */
#define CHRLC   036           /*  "    "  lower-case */
#define CHRLF   033           /* line-feed */
#define CHRCR   034           /* carriage-return */
#define CHRSP   040           /* space (identity!) */

/****************
 * Vector and Vector Continue Words
 */

#define ESCP    0400000         /* Escape */
#define INSFY   0200000         /* Intensify */

/** y position */

#define DN      0100000         /* Down */
#define UP      0000000         /* Up */

/* number of points moved */
#define YP64    0040000
#define YP32    0020000
#define YP16    0010000
#define YP8     0004000
#define YP4     0002000
#define YP2     0001000
#define YP1     0000400

/** x position */
#define LT      0000200         /* Left */
#define RT      0000000         /* Right */

#define XP64    0000100
#define XP32    0000040
#define XP16    0000020
#define XP8     0000010
#define XP4     0000004
#define XP2     0000002
#define XP1     0000001

/****************
 * increment
 */

#define PR      010
#define PL      014
#define PU      002
#define PD      003

#define PUL     (PU|PL)
#define PUR     (PU|PR)

#define PDL     (PD|PL)
#define PDR     (PD|PR)

/* INSFY and ESCP from vector modes */
