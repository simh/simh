/* Generic Card read/punch routines for simulators.

   Copyright (c) 2005, Richard Cornwell

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This is the standard card reader.
   This is the standard card punch.

   Input formats are accepted in a variaty of formats:
        Standard ASCII: one record per line.
                returns are ignored.
                tabs are expanded to modules 8 characters.
                ~ in first column is treated as a EOF.

        Binary Card format:
                Each record 160 characters.
                First characters 6789----
                Second character 21012345
                                 111
                Top 4 bits of second character are 0.
                It is unlikely that any other format could
                look like this.

        BCD Format:
                Each record variable length (80 chars or less).
                Record mark has bit 7 set.
                Bit 6 is even parity.
                Bits 5-0 are character.

        CBN Format:
                Each record 160 charaters.
                First char has bit 7 set. Rest set to 0.
                Bit 6 is odd parity.
                Bit 5-0 of first character are top 6 bits
                        of card.
                Bit 5-0 of second character are lower 6 bits
                        of card.

    For autodetection of card format, there can be no parity errors.
    All undeterminate formats are treated as ASCII.

    Auto output format is ASCII if card has only printable characters
    or card format binary.

    The card module uses up7 to hold a buffer for the card being translated
    and the backward translation table. Which is generated from the table.
*/


#include <ctype.h>
#include "sim_defs.h"
#include "sim_card.h"

#if defined(USE_SIM_CARD)

#define card_ctx up8

#define CARD_EOF          0x1000         /* This card is end of file card. */
#define CARD_ERR          0x2000         /* Return error for this card */
#define DECK_SIZE         1000           /* Number of cards to allocate at a time */


struct card_context
{
    t_addr              punch_count;     /* Number of cards punched */
    char                cbuff[1024];     /* Read in buffer for cards */
    uint8               hol_to_ascii[4096]; /* Back conversion table */
    t_addr              hopper_size;     /* Size of hopper */
    t_addr              hopper_cards;    /* Number of cards in hopper */
    uint16              (*images)[1][80];
};

/* Character conversion tables */

const char          sim_six_to_ascii[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '=', '\'', ':', '>', '%',    /* 17 = box */
    '_', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '@', ',', '(', '~', '\\', '#',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '^',     /* 57 = triangle */
    '+', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '@',     /* 37 = stop code */
};                              /* 72 = rec mark */
                                /* 75 = squiggle, 77 = del */

static const uint16          ascii_to_hol_026[128] = {
   /* Control                              */
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,    /*0-37*/
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*  sp      !      "      #      $      %      &      ' */
   /* none   Y28    78     T28    Y38    T48    XT     48  */
    0x000, 0x600, 0x006, 0x282, 0x442, 0x222, 0xA00, 0x022,     /* 40 - 77 */
   /*   (      )      *      +      ,      -      .      / */
   /* T48    X48    Y48    X      T38    T      X38    T1  */
    0x222, 0x822, 0x422, 0x800, 0x242, 0x400, 0x842, 0x300,
   /*   0      1      2      3      4      5      6      7 */
   /* T      1      2      3      4      5      6      7   */
    0x200, 0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004,
   /*   8      9      :      ;      <      =      >      ? */
   /* 8      9      58     Y68    X68    38     68     X28 */
    0x002, 0x001, 0x012, 0x40A, 0x80A, 0x042, 0x00A, 0x882,
   /*   @      A      B      C      D      E      F      G */
   /*  82    X1     X2     X3     X4     X5     X6     X7  */
    0x022, 0x900, 0x880, 0x840, 0x820, 0x810, 0x808, 0x804,     /* 100 - 137 */
   /*   H      I      J      K      L      M      N      O */
   /* X8     X9     Y1     Y2     Y3     Y4     Y5     Y6  */
    0x802, 0x801, 0x500, 0x480, 0x440, 0x420, 0x410, 0x408,
   /*   P      Q      R      S      T      U      V      W */
   /* Y7     Y8     Y9     T2     T3     T4     T5     T6  */
    0x404, 0x402, 0x401, 0x280, 0x240, 0x220, 0x210, 0x208,
   /*   X      Y      Z      [      \      ]      ^      _ */
   /* T7     T8     T9     X58    X68    T58    T78     28 */
    0x204, 0x202, 0x201, 0x812, 0x20A, 0x412, 0x406, 0x082,
   /*   `      a      b      c      d      e      f      g */
    0x212, 0xB00, 0xA80, 0xA40, 0xA20, 0xA10, 0xA08, 0xA04,     /* 140 - 177 */
   /*   h      i      j      k      l      m      n      o */
    0xA02, 0xA01, 0xD00, 0xC80, 0xC40, 0xC20, 0xC10, 0xC08,
   /*   p      q      r      s      t      u      v      w */
    0xC04, 0xC02, 0xC01, 0x680, 0x640, 0x620, 0x610, 0x608,
   /*   x      y      z      {      |      }      ~    del */
   /*                     T79     Y78   Y79     79         */
    0x604, 0x602, 0x601, 0x406, 0x806, 0x805, 0x005, 0xf000
};

/* Set for Burrough codes */
static const uint16          ascii_to_hol_029[128] = {
   /* Control                              */
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,    /*0-37*/
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*  sp      !      "      #      $      %      &      ' */
   /* none   X28    78      38    Y38    T48    X      58  */
    0x000, 0x482, 0x006, 0x042, 0x442, 0x222, 0x800, 0x012,     /* 40 - 77 */
   /*   (      )      *      +      ,      -      .      / */
   /* X58    Y58    Y48    X68    T38    Y      X38    T1  */
    0x812, 0x412, 0x422, 0x80A, 0x242, 0x400, 0x842, 0x300,
   /*   0      1      2      3      4      5      6      7 */
   /* T      1      2      3      4      5      6      7   */
    0x200, 0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004,
   /*   8      9      :      ;      <      =      >      ? */
   /* 8      9      28     Y68    X48     68    T68     28 */
    0x002, 0x001, 0x082, 0x40A, 0x822, 0x00A, 0x20A, 0x206,
   /*   @      A      B      C      D      E      F      G */
   /*  48    X1     X2     X3     X4     X5     X6     X7  */
    0x022, 0x900, 0x880, 0x840, 0x820, 0x810, 0x808, 0x804,     /* 100 - 137 */
   /*   H      I      J      K      L      M      N      O */
   /* X8     X9     Y1     Y2     Y3     Y4     Y5     Y6  */
    0x802, 0x801, 0x500, 0x480, 0x440, 0x420, 0x410, 0x408,
   /*   P      Q      R      S      T      U      V      W */
   /* Y7     Y8     Y9     T2     T3     T4     T5     T6  */
    0x404, 0x402, 0x401, 0x280, 0x240, 0x220, 0x210, 0x208,
   /*   X      Y      Z      [      \      ]      ^      _ */
   /* T7     T8     T9     X0     T28    Y28    T78    T58 */
    0x204, 0x202, 0x201, 0xA00, 0x282, 0x882, 0x406, 0x212,
   /*   `      a      b      c      d      e      f      g */
    0xf000,0xB00, 0xA80, 0xA40, 0xA20, 0xA10, 0xA08, 0xA04,     /* 140 - 177 */
   /*   h      i      j      k      l      m      n      o */
    0xA02, 0xA01, 0xD00, 0xC80, 0xC40, 0xC20, 0xC10, 0xC08,
   /*   p      q      r      s      t      u      v      w */
    0xC04, 0xC02, 0xC01, 0x680, 0x640, 0x620, 0x610, 0x608,
   /*   x      y      z      {      |      }      ~    del */
   /*                     Y78     YT     78    X78         */
    0x604, 0x602, 0x601, 0x405, 0x600, 0x805, 0x806,0xf000
};

static const uint16          ascii_to_hol_ebcdic[128] = {
   /* Control                              */
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,    /*0-37*/
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*  sp      !      "      #      $      %      &      ' */
   /* none   Y28    78      38    Y38    T48    X      58  */
    0x000, 0x482, 0x006, 0x042, 0x442, 0x222, 0x800, 0x012,     /* 40 - 77 */
   /*   (      )      *      +      ,      -      .      / */
   /* X58    Y58    Y48    X      T38    Y      X38    T1  */
    0x812, 0x412, 0x422, 0x800, 0x242, 0x400, 0x842, 0x300,
   /*   0      1      2      3      4      5      6      7 */
   /* T      1      2      3      4      5      6      7   */
    0x200, 0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004,
   /*   8      9      :      ;      <      =      >      ? */
   /* 8      9      28     Y68    X48    68     T68    T78 */
    0x002, 0x001, 0x082, 0x40A, 0x822, 0x00A, 0x20A, 0x206,
   /*   @      A      B      C      D      E      F      G */
   /*  48    X1     X2     X3     X4     X5     X6     X7  */
    0x022, 0x900, 0x880, 0x840, 0x820, 0x810, 0x808, 0x804,     /* 100 - 137 */
   /*   H      I      J      K      L      M      N      O */
   /* X8     X9     Y1     Y2     Y3     Y4     Y5     Y6  */
    0x802, 0x801, 0x500, 0x480, 0x440, 0x420, 0x410, 0x408,
   /*   P      Q      R      S      T      U      V      W */
   /* Y7     Y8     Y9     T2     T3     T4     T5     T6  */
    0x404, 0x402, 0x401, 0x280, 0x240, 0x220, 0x210, 0x208,
   /*   X      Y      Z      [      \      ]      ^      _ */
   /* T7     T8     T9     X28    X68    T28    T78    X58 */
    0x204, 0x202, 0x201, 0x882, 0x20A, 0x482, 0x406, 0x212,
   /*   `      a      b      c      d      e      f      g */
    0x212, 0xB00, 0xA80, 0xA40, 0xA20, 0xA10, 0xA08, 0xA04,     /* 140 - 177 */
   /*   h      i      j      k      l      m      n      o */
    0xA02, 0xA01, 0xD00, 0xC80, 0xC40, 0xC20, 0xC10, 0xC08,
   /*   p      q      r      s      t      u      v      w */
    0xC04, 0xC02, 0xC01, 0x680, 0x640, 0x620, 0x610, 0x608,
   /*   x      y      z      {      |      }      ~    del */
   /*                     Y78     X78    78     79         */
    0x604, 0x602, 0x601, 0x406, 0x806,0x0006,0x0005,0xf000
};

const char          sim_ascii_to_six[128] = {
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,     /* 0 - 37 */
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /*sp    !    "    #    $    %    &    ' */
    000, 052,  -1, 032, 053, 017, 060, 014,     /* 40 - 77 */
   /* (    )    *    +    ,    -    .    / */
    034, 074, 054, 060, 033, 040, 073, 021,
   /* 0    1    2    3    4    5    6    7 */
    012, 001, 002, 003, 004, 005, 006, 007,
   /* 8    9    :    ;    <    =    >    ? */
    010, 011, 015, 056, 076, 013, 016, 072,
   /* @    A    B    C    D    E    F    G */
    014, 061, 062, 063, 064, 065, 066, 067,     /* 100 - 137 */
   /* H    I    J    K    L    M    N    O */
    070, 071, 041, 042, 043, 044, 045, 046,
   /* P    Q    R    S    T    U    V    W */
    047, 050, 051, 022, 023, 024, 025, 026,
   /* X    Y    Z    [    \    ]    ^    _ */
    027, 030, 031, 075, 036, 055, 057, 020,
   /* `    a    b    c    d    e    f    g */
    035, 061, 062, 063, 064, 065, 066, 067,     /* 140 - 177 */
   /* h    i    j    k    l    m    n    o */
    070, 071, 041, 042, 043, 044, 045, 046,
   /* p    q    r    s    t    u    v    w */
    047, 050, 051, 022, 023, 024, 025, 026,
   /* x    y    z    {    |    }    ~   del*/
    027, 030, 031, 057, 077, 017,  -1,  -1
};

static uint16 ebcdic_to_hol[256] = {
 /*  T0918   T91    T92    T93    T94    T95    T96   T97   0x0x */
   0xB03,  0x901, 0x881, 0x841, 0x821, 0x811, 0x809, 0x805,
 /*  T98,    T918 , T928,  T938,  T948,  T958,  T968, T978   */
   0x803,  0x903, 0x883, 0x843, 0x823, 0x813, 0x80B, 0x807,
 /*  TE918   E91    E92    E93    E94    E95    E96   E97   0x1x */
   0xD03,  0x501, 0x481, 0x441, 0x421, 0x411, 0x409, 0x405,
 /*  E98     E918   E928   E938   E948   E958   E968  E978   */
   0x403,  0x503, 0x483, 0x443, 0x423, 0x413, 0x40B, 0x407,
 /*  E0918   091    092    093    094    095    096   097   0x2x */
   0x703,  0x301, 0x281, 0x241, 0x221, 0x211, 0x209, 0x205,
 /*  098     0918  0928   0938    0948   0958   0968  0978   */
   0x203,  0x303, 0x283, 0x243, 0x223, 0x213, 0x20B, 0x207,
 /* TE0918   91    92     93      94     95     96     97   0x3x */
   0xF03,  0x101, 0x081, 0x041, 0x021, 0x011, 0x009, 0x005,
 /*  98      918   928    938     948    958   968     978   */
   0x003,  0x103, 0x083, 0x043, 0x023, 0x013, 0x00B, 0x007,
 /*          T091  T092   T093   T094   T095   T096    T097  0x4x */
   0x000,  0xB01, 0xA81, 0xA41, 0xA21, 0xA11, 0xA09, 0xA05,
 /* T098     T18   T28    T38    T48    T58    T68     T78    */
   0xA03,  0x902, 0x882, 0x842, 0x822, 0x812, 0x80A, 0x806,
 /* T        TE91  TE92   TE93   TE94   TE95   TE96    TE97  0x5x */
   0x800,  0xD01, 0xC81, 0xC41, 0xC21, 0xC11, 0xC09, 0xC05,
 /* TE98     E18   E28    E38    E48    E58    E68     E78    */
   0xC03,  0x502, 0x482, 0x442, 0x422, 0x412, 0x40A, 0x406,
 /* E        01    E092   E093   E094   E095   E096    E097  0x6x */
   0x400,  0x300, 0x681, 0x641, 0x621, 0x611, 0x609, 0x605,
 /* E098     018   TE     038    048     68    068     078    */
   0x603,  0x302, 0xC00, 0x242, 0x222, 0x212, 0x20A, 0x206,
 /* TE0    TE091  TE092  TE093  TE094  TE095  TE096  TE097   0x7x */
   0xE00,  0xF01, 0xE81, 0xE41, 0xE21, 0xE11, 0xE09, 0xE05,
 /* TE098    18     28     38    48      58      68     78    */
   0xE03,  0x102, 0x082, 0x042, 0x022, 0x012, 0x00A, 0x006,
 /* T018     T01    T02    T03    T04    T05    T06    T07   0x8x */
   0xB02,  0xB00, 0xA80, 0xA40, 0xA20, 0xA10, 0xA08, 0xA04,
 /* T08      T09   T028   T038    T048   T058   T068   T078   */
   0xA02,  0xA01, 0xA82, 0xA42, 0xA22, 0xA12, 0xA0A, 0xA06,
 /* TE18     TE1    TE2    TE3    TE4    TE5    TE6    TE7   0x9x */
   0xD02,  0xD00, 0xC80, 0xC40, 0xC20, 0xC10, 0xC08, 0xC04,
 /* TE8      TE9   TE28   TE38    TE48   TE58   TE68   TE78   */
   0xC02,  0xC01, 0xC82, 0xC42, 0xC22, 0xC12, 0xC0A, 0xC06,
 /* E018     E01    E02    E03    E04    E05    E06    E07   0xax */
   0x702,  0x700, 0x680, 0x640, 0x620, 0x610, 0x608, 0x604,
 /* E08      E09   E028   E038    E048   E058   E068   E078   */
   0x602,  0x601, 0x682, 0x642, 0x622, 0x612, 0x60A, 0x606,
 /* TE018    TE01   TE02   TE03   TE04   TE05   TE06   TE07  0xbx */
   0xF02,  0xF00, 0xE80, 0xE40, 0xE20, 0xE10, 0xE08, 0xE04,
 /* TE08     TE09   TE028  TE038  TE048  TE058  TE068  TE078  */
   0xE02,  0xE01, 0xE82, 0xE42, 0xE22, 0xE12, 0xE0A, 0xE06,
 /* T0       T1     T2     T3     T4     T5     T6     T7    0xcx */
   0xA00,  0x900, 0x880, 0x840, 0x820, 0x810, 0x808, 0x804,
 /* T8       T9     T0928  T0938  T0948  T0958  T0968  T0978  */
   0x802,  0x801, 0xA83, 0xA43, 0xA23, 0xA13, 0xA0B, 0xA07,
 /* E0       E1     E2     E3     E4     E5     E6     E7    0xdx */
   0x600,  0x500, 0x480, 0x440, 0x420, 0x410, 0x408, 0x404,
 /* E8       E9     TE928  TE938  TE948  TE958  TE968  TE978  */
   0x402,  0x401, 0xC83, 0xC43, 0xC23, 0xC13, 0xC0B, 0xC07,
 /* 028      E091   02     03     04     05     06     07    0xex  */
   0x182,  0x701, 0x280, 0x240, 0x220, 0x210, 0x208, 0x204,
 /* 08       09     E0928  E0938  E0948  E0958  E0968  E0978  */
   0x202,  0x201, 0x683, 0x643, 0x623, 0x613, 0x60B, 0x607,
 /* 0        1      2      3      4      5      6      7     0xfx */
   0x200,  0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004,
 /* 8        9     TE0928 TE0938 TE0948 TE0958 TE0968 TE0978  */
   0x002,  0x001, 0xE83, 0xE43, 0xE23, 0xE13, 0xE0B, 0xE07
};

static uint16 hol_to_ebcdic[4096];

const uint8        sim_parity_table[64] = {
    /* 0    1    2    3    4    5    6    7 */
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000
};

struct card_formats {
    uint32      mode;
    const char  *name;
};

static struct card_formats fmts[] = {
    {MODE_AUTO,  "AUTO"},
    {MODE_BIN,   "BIN"},
    {MODE_TEXT,  "TEXT"},
    {MODE_BCD,   "BCD"},
    {MODE_CBN,   "CBN"},
    {MODE_EBCDIC,"EBCDIC"},
    {MODE_OCTAL, "OCTAL"},
    {0, 0},
};


/* Conversion routines */

/* Convert BCD character into hollerith code */
uint16
sim_bcd_to_hol(uint8 bcd) {
    uint16      hol;

    /* Handle space correctly */
    if (bcd == 0)               /* 0 to 82 punch */
        return 0x82;
    if (bcd == 020)             /* 20 no punch */
        return 0;

    /* Convert to top column */
    switch (bcd & 060) {
    default:
    case 000:
         hol = 0x000;
         break;
    case 020:
         hol = 0x200;
         break;
    case 040:
         hol = 0x400;
         break;
    case 060:
         hol = 0x800;
         break;
    }

    /* Handle case of 10 special */
    /* Only 032 is punched as 8-2 */
    if ((bcd & 017) == 10 && (bcd & 060) != 020) {
        hol |= 1 << 9;
        return hol;
    }
    /* Convert to 0-9 row */
    bcd &= 017;
    if (bcd > 9) {
        hol |= 0x2;       /* Col 8 */
        bcd -= 8;
    }
    if (bcd != 0)
        hol |= 1 << (9 - bcd);
    return hol;
}

/* Returns the BCD of the hollerith code or 0x7f if error */
uint8
sim_hol_to_bcd(uint16 hol) {
    uint8                bcd;

    /* Convert 10,11,12 rows */
    switch (hol & 0xe00) {
    case 0x000:
         bcd = 0;
         break;
    case 0x200:
         if ((hol & 0x1ff) == 0)
           return 10;
         bcd = 020;
         break;
    case 0x400:
         bcd = 040;
         break;
    case 0x600: /* 11-10 Punch */
         bcd = 052;
         break;
    case 0x800:
         bcd = 060;
         break;
    case 0xA00: /* 12-10 Punch */
         bcd = 072;
         break;
    default: /* Double punch in 10,11,12 rows */
         return 0x7f;
    }

    hol &= 0x1ff;       /* Mask rows 0-9 */
    /* Check row 8 punched */
    if (hol & 0x2) {
         bcd += 8;
         hol &= ~0x2;
    }

    /* Convert rows 0-9 */
    while (hol != 0 && (hol & 0x200) == 0) {
         bcd++;
         hol <<= 1;
    }

    /* Any more columns punched? */
    if ((hol & 0x1ff) != 0)
         return 0x7f;
    return bcd;
}

/* Convert EBCDIC character into hollerith code */
uint16
sim_ebcdic_to_hol(uint8 ebcdic) {
   return ebcdic_to_hol[ebcdic];
}



/* Returns the BCD of the hollerith code or 0x7f if error */
uint16
sim_hol_to_ebcdic(uint16 hol) {
    return hol_to_ebcdic[hol];
}



t_addr
sim_hopper_size(UNIT * uptr) {
    struct card_context  *data = (struct card_context *)uptr->card_ctx;
    if (data == NULL)
        return 0;
    return data->hopper_cards;
}

t_addr
sim_punch_count(UNIT * uptr) {
    struct card_context  *data = (struct card_context *)uptr->card_ctx;
    if (data == NULL)
        return 0;
    return data->punch_count;
}

t_addr
sim_card_input_hopper_count(UNIT *uptr) {
    struct card_context  *data = (struct card_context *)uptr->card_ctx;
    uint16                col;

    if (data == NULL || data->images == NULL)
        return 0;           /* attached? */

    if (uptr->pos >= data->hopper_cards)
        return 0;

    col = (*data->images)[data->hopper_cards-1][0];

    return (int)((data->hopper_cards - uptr->pos) - ((col & CARD_EOF) ? 1 : 0));
}

t_addr
sim_card_output_hopper_count(UNIT *uptr) {
    struct card_context  *data = (struct card_context *)uptr->card_ctx;

    if (data == NULL)
        return 0;           /* attached? */

    return (int)data->punch_count;
}


t_cdstat
sim_read_card(UNIT * uptr, uint16 image[80])
{
    int                   i;
    struct card_context  *data = (struct card_context *)uptr->card_ctx;
    DEVICE               *dptr;
    uint16               (*img)[80];
    t_stat                r = CDSE_OK;

    if (data == NULL || (uptr->flags & UNIT_ATT) == 0)
        return CDSE_EMPTY;      /* attached? */
    if (data->hopper_cards == 0 || uptr->pos >= data->hopper_cards)
        return CDSE_EMPTY;

    dptr = find_dev_from_unit( uptr);
    img = &(*data->images)[uptr->pos];
    if (sim_deb && dptr && ((dptr)->dctrl & DEBUG_CARD)) {
         if (image[0] & CARD_EOF) {
             sim_debug(DEBUG_CARD, dptr, "Read hopper EOF\n");
         } else if (image[0] & CARD_ERR) {
             sim_debug(DEBUG_CARD, dptr, "Read hopper ERR\n");
         } else {
             uint8        out[81];
             int          ok = 1;
             for (i = 0; i < 80; i++) {
                 out[i] = data->hol_to_ascii[(int)(*img)[i]];
                 if (out[i] == 0xff) {
                    ok = 0;
                 }
             }
             if (ok) {
                 sim_debug(DEBUG_CARD, dptr, "Read hopper: [");
                 for (i = 0; i < 80; i++) {
                     sim_debug(DEBUG_CARD, dptr, "%c", out[i]);
                 }
                 sim_debug(DEBUG_CARD, dptr, "]\n");
             } else {
                 sim_debug(DEBUG_CARD, dptr, "Read hopper binary\n");
             }
         }
    }
    if ((*img)[0] & CARD_EOF)
        r = CDSE_EOF;
    else if ((*img)[0] & CARD_ERR)
           r = CDSE_ERROR;
    uptr->pos++;
    data->punch_count++;
    memcpy(image, img, 80 * sizeof(uint16));
    image[0] &= 0xfff;          /* Remove any CARD_EOF and CARD_ERR Flags */
    return r;
}

/*
 * Check if reader is at last card.
 */
int
sim_card_eof(UNIT *uptr)
{
    struct card_context  *data = (struct card_context *)uptr->card_ctx;
    uint16                col;

    if (data == NULL || data->images == NULL)
        return SCPE_UNATT;      /* attached? */

    if (uptr->pos >= data->hopper_cards)
        return SCPE_UNATT;

    col = (*data->images)[uptr->pos][0];

    if (col & CARD_EOF)
        return 1;
    return 0;
}



struct _card_buffer {
   uint8                 buffer[8192+500];    /* Buffer data */
   int                   len;                 /* Amount of data in buffer */
   int                   size;                /* Size of last card read */
};

static int _cmpcard(const uint8 *p, const char *s) {
   int  i;
   if (p[0] != '~')
        return 0;
   for(i = 0; i < 3; i++) {
        if (tolower(p[i+1]) != s[i])
           return 0;
   }
   return 1;
}

t_stat
_sim_parse_card(UNIT *uptr, DEVICE *dptr, struct _card_buffer *buf, uint16 (*image)[80]) {
    int                   mode;
    uint16                temp;
    int                   i;
    char                  c;
    int                   col;

    sim_debug(DEBUG_CARD, dptr, "Read card ");
    if ((uptr->flags & UNIT_CARD_MODE) == MODE_AUTO) {
        mode = MODE_TEXT;   /* Default is text */

        /* Check buffer to see if binary card in it. */
        for (i = 0, temp = 0; i < 160 && i <buf->len; i+=2)
            temp |= (uint16)(buf->buffer[i] & 0xFF);
        /* Check if every other char < 16 & full buffer */
        if ((temp & 0x0f) == 0 && i == 160)
            mode = MODE_BIN;        /* Probably binary */
        /* Check if maybe BCD or CBN */
        if (buf->buffer[0] & 0x80) {
            int     odd = 0;
            int     even = 0;

            /* Clear record mark */
            buf->buffer[0] &= 0x7f;
            /* Check all chars for correct parity */
            for(i = 0, temp = 0; i < buf->len; i++) {
               uint8        ch = buf->buffer[i];
               /* Stop at EOR */
               if (ch & 0x80)
                   break;
               /* Try matching parity */
               if (sim_parity_table[(ch & 077)] == (ch & 0100))
                    even++;
               else
                    odd++;
           }
           /* Restore it */
           buf->buffer[0] |= 0x80;
           if (i == 160 && odd == i)
               mode = MODE_CBN;
           else if (i < 80 && even == i)
               mode = MODE_BCD;
        }

        /* Check if modes match */
        if ((uptr->flags & UNIT_CARD_MODE) != MODE_AUTO &&
            (uptr->flags & UNIT_CARD_MODE) != mode) {
            (*image)[0] = CARD_ERR;
            sim_debug(DEBUG_CARD, dptr, "invalid mode\n");
            return SCPE_OPENERR;
        }
    } else
        mode = uptr->flags & UNIT_CARD_MODE;

    switch(mode) {
    default:
    case MODE_TEXT:
        sim_debug(DEBUG_CARD, dptr, "text: [");
        /* Check for special codes */
        if (buf->buffer[0] == '~') {
            int f = 1;
            for(col = i = 1; col < 80 && f && i < buf->len; i++) {
                c = buf->buffer[i];
                switch (c) {
                case '\n':
                case '\0':
                case '\r':
                    col = 80;
                case ' ':
                    break;              /* Ignore these */
                case '\t':
                    col = (col | 7) + 1;        /* Mult of 8 */
                    break;
                default:
                    f = 0;
                    break;
                }
             }
             if (f) {
                (*image)[0] |= CARD_EOF;
                goto end_card;
             }
        }
        if (_cmpcard(&buf->buffer[0], "raw")) {
            int         j = 0;
            sim_debug(DEBUG_CARD, dptr, "-octal-");
            for(col = 0, i = 4; col < 80 && i < buf->len; i++) {
                if (buf->buffer[i] >= '0' && buf->buffer[i] <= '7') {
                    (*image)[col] = ((*image)[col] << 3) | (buf->buffer[i] - '0');
                    j++;
                } else if (buf->buffer[i] == '\n' || buf->buffer[i] == '\r') {
                    break;
                } else {
                    (*image)[0] = CARD_ERR;
                    break;
                }
                if (j == 4) {
                   col++;
                   j = 0;
                }
            }
        } else if (_cmpcard(&buf->buffer[0], "eor")) {
            sim_debug(DEBUG_CARD, dptr, "-eor-");
            (*image)[0] = 07;        /* 7/8/9 punch */
            i = 4;
        } else if (_cmpcard(&buf->buffer[0], "eof")) {
            sim_debug(DEBUG_CARD, dptr, "-eof-");
            (*image)[0] = 015;       /* 6/7/9 punch */
            i = 4;
        } else if (_cmpcard(&buf->buffer[0], "eoi")) {
            sim_debug(DEBUG_CARD, dptr, "-eoi-");
            (*image)[0] = 017;       /* 6/7/8/9 punch */
            i = 4;
        } else {
            /* Convert text line into card image */
            for (col = 0, i = 0; col < 80 && i < buf->len; i++) {
                c = buf->buffer[i];
                switch (c) {
                case '\0':
                case '\r':
                    break;              /* Ignore these */
                case '\t':
                    col = (col | 7) + 1;        /* Mult of 8 */
                    break;
                case '\n':
                    col = 80;
                    i--;
                    break;
                default:
                    sim_debug(DEBUG_CARD, dptr, "%c", c);
                    if ((uptr->flags & MODE_LOWER) == 0)
                        c = toupper(c);
                    switch(uptr->flags & MODE_CHAR) {
                    default:
                    case MODE_026:
                           temp = ascii_to_hol_026[(int)c];
                           break;
                    case MODE_029:
                           temp = ascii_to_hol_029[(int)c];
                           break;
                    case MODE_EBCDIC:
                           temp = ascii_to_hol_ebcdic[(int)c];
                           break;
                    }
                    if (temp & 0xf000)
                        (*image)[0] |= CARD_ERR;
                    (*image)[col++] = temp & 0xfff;
                }
            }
        }
    end_card:
        sim_debug(DEBUG_CARD, dptr, "-%d-", i);

        /* Scan to end of line, ignore anything after last column */
        while (buf->buffer[i] != '\n' && buf->buffer[i] != '\r' && i < buf->len) {
            i++;
        }
        if (buf->buffer[i] == '\r')
            i++;
        if (buf->buffer[i] == '\n')
            i++;
        sim_debug(DEBUG_CARD, dptr, "]\n");
        break;

    case MODE_BIN:
        temp = 0;
        sim_debug(DEBUG_CARD, dptr, "bin\n");
        if (buf->len < 160) {
            (*image)[0] = CARD_ERR;
            return SCPE_OPENERR;
        }
        /* Move data to buffer */
        for (col = i = 0; i < 160;) {
            temp |= (uint16)(buf->buffer[i] & 0xff);
            (*image)[col] = (buf->buffer[i++] >> 4) & 0xF;
            (*image)[col++] |= ((uint16)buf->buffer[i++] & 0xf) << 4;
        }
        /* Check if format error */
        if (temp & 0xF)
            (*image)[0]  |= CARD_ERR;

        break;

    case MODE_CBN:
        sim_debug(DEBUG_CARD, dptr, "cbn\n");
        /* Check if first character is a tape mark */
        if (buf->buffer[0] == 0217 &&
                   (buf->len == 1 || (buf->buffer[1] & 0200) != 0)) {
            i = 1;
            (*image)[0] |= CARD_EOF;
            break;
        }

        /* Clear record mark */
        buf->buffer[0] &= 0x7f;

        /* Convert card and check for errors */
        for (col = i = 0; i < buf->len && col < 80;) {
            uint8       c;

            if (buf->buffer[i] & 0x80)
                break;
            c = buf->buffer[i] & 077;
            if (sim_parity_table[(int)c] == (buf->buffer[i++] & 0100))
                (*image)[0] |= CARD_ERR;
            (*image)[col] = ((uint16)c) << 6;
            if (buf->buffer[i] & 0x80)
                break;
            c = buf->buffer[i] & 077;
            if (sim_parity_table[(int)c] == (buf->buffer[i++] & 0100))
                (*image)[0] |= CARD_ERR;
            (*image)[col++] |= c;
        }

        if (i < buf->len && col >= 80 && (buf->buffer[i] & 0x80) == 0) {
           (*image)[0] |= CARD_ERR;
        }
        /* Record over length of card, skip until next */
        while ((buf->buffer[i] & 0x80) == 0) {
            if (i > buf->len)
               break;
            i++;
        }
        break;

    case MODE_BCD:
        sim_debug(DEBUG_CARD, dptr, "bcd [");
        /* Check if first character is a tape mark */
        if (buf->buffer[0] == 0217 && (buf->buffer[1] & 0200) != 0) {
            i = 1;
            (*image)[0] |= CARD_EOF;
            break;
        }

        /* Clear record mark */
        buf->buffer[0] &= 0x7f;

        /* Convert text line into card image */
        for (col = 0, i = 0; col < 80 && i < buf->len; i++) {
            if (buf->buffer[i] & 0x80)
                break;
            c = buf->buffer[i] & 077;
            if (sim_parity_table[(int)c] != (buf->buffer[i] & 0100))
                (*image)[0] |= CARD_ERR;
            sim_debug(DEBUG_CARD, dptr, "%c", sim_six_to_ascii[(int)c]);
            /* Convert to top column */
            (*image)[col++] = sim_bcd_to_hol(c);
        }

        if (i < buf->len && col >= 80 && (buf->buffer[i] & 0x80) == 0) {
           (*image)[0] |= CARD_ERR;
        }

        /* Record over length of card, skip until next */
        while ((buf->buffer[i] & 0x80) == 0) {
            if (i > buf->len)
               break;
            i++;
        }

        sim_debug(DEBUG_CARD, dptr, "]\n");
        break;

    case MODE_EBCDIC:
        sim_debug(DEBUG_CARD, dptr, "ebcdic\n");
        if (buf->len < 80)
            (*image)[0] |= CARD_ERR;
        /* Move data to buffer */
        for (i = 0; i < 80 && i < buf->len; i++) {
            temp = (uint16)(buf->buffer[i]) & 0xFF;
            (*image)[i] = ebcdic_to_hol[temp];
        }
        break;

    }
    buf->size = i;
    return SCPE_OK;
}

t_stat
_sim_read_deck(UNIT * uptr, int eof)
{
    struct _card_buffer   buf;
    struct card_context  *data;
    DEVICE               *dptr;
    int                   i;
    int                   j;
    int                   l;
    int                   cards = 0;
    t_stat                r = SCPE_OK;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    dptr = find_dev_from_unit( uptr);
    data = (struct card_context *)uptr->card_ctx;

    buf.len = 0;
    buf.size = 0;
    buf.buffer[0] = 0; /* Initialize bufer to empty */

    /* Slurp up current file */
    do {
        if (buf.len < 500 && !feof(uptr->fileref)) {
            l = sim_fread(&buf.buffer[buf.len], 1, 8192, uptr->fileref);
            if (l < 0)
                r = SCPE_OPENERR;
            else
                buf.len += l;
        }

        /* Allocate space for some more cards if needed */
        if (data->hopper_cards >= data->hopper_size) {
            data->hopper_size += DECK_SIZE;
            data->images = (uint16 (*)[1][80])realloc(data->images,
                       data->hopper_size * sizeof(*(data->images)));
            memset(&data->images[data->hopper_cards], 0,
                       (data->hopper_size - data->hopper_cards) *
                             sizeof(*(data->images)));
        }

        /* Process one card */
        cards++;
        if (_sim_parse_card(uptr, dptr, &buf, &(*data->images)[data->hopper_cards])
                != SCPE_OK) {
            r = sim_messagef(SCPE_OPENERR, "%s: %s Error (%s) in card %d\n",
                   sim_uname(uptr), uptr->filename, sim_error_text(r), cards);
        }
        data->hopper_cards++;
        /* Move data to start at begining of buffer */
        /* Data is moved down to simplify the decoding of one card */
        l = buf.len - buf.size;
        j = buf.size;
        for(i = 0; i < l; i++, j++)
            buf.buffer[i] = buf.buffer[j];
        buf.len -= buf.size;
    } while (buf.len > 0 && r == SCPE_OK);

    /* If there is an error, free just read deck */
    if (r == SCPE_OK) {
       if (eof) {
          /* Allocate space for some more cards if needed */
          if (data->hopper_cards >= data->hopper_size) {
              data->hopper_size += DECK_SIZE;
              data->images = (uint16 (*)[1][80])realloc(data->images,
                         data->hopper_size * sizeof(*(data->images)));
              memset(&data->images[data->hopper_cards], 0,
                         (data->hopper_size - data->hopper_cards) *
                               sizeof(*(data->images)));
          }

          /* Create empty card */
          (*data->images)[data->hopper_cards][0] = CARD_EOF;
          data->hopper_cards++;
       }
    }
    return r;
}


/* Card punch routine

   Modifiers have been checked by the caller
   C modifier is recognized (column binary is implemented)
*/


t_stat
sim_punch_card(UNIT * uptr, uint16 image[80])
{
/* Convert word record into column image */
/* Check output type, if auto or text, try and convert record to bcd first */
/* If failed and text report error and dump what we have */
/* Else if binary or not convertable, dump as image */

    /* Try to convert to text */
    uint8                out[512];
    int                  i;
    int                  outp = 0;
    int                  mode = uptr->flags & UNIT_CARD_MODE;
    int                  ok = 1;
    struct card_context *data;
    DEVICE              *dptr;

    dptr = find_dev_from_unit(uptr);
    data = (struct card_context *)uptr->card_ctx;

    if (data == NULL || (uptr->flags & UNIT_ATT) == 0)
        return CDSE_EMPTY;      /* attached? */

    /* Fix mode if in auto mode */
    if (mode == MODE_AUTO) {
         /* Try to convert each column to ascii */
         for (i = 0; i < 80; i++) {
             out[i] = data->hol_to_ascii[image[i]];
             if (out[i] == 0xff) {
                ok = 0;
             }
         }
         mode = ok?MODE_TEXT:MODE_OCTAL;
    }

    switch(mode) {
    default:
    case MODE_TEXT:
        /* Scan each column */
        sim_debug(DEBUG_CARD, dptr, "text: [");
        for (i = 0; i < 80; i++, outp++) {
            out[outp] = data->hol_to_ascii[image[i]];
            if (out[outp] == 0xff) {
               out[outp] = '?';
            }
            sim_debug(DEBUG_CARD, dptr, "%c", out[outp]);
        }
        sim_debug(DEBUG_CARD, dptr, "]\n");
        /* Trim off trailing spaces */
        while (outp > 0 && out[--outp] == ' ') ;
        out[++outp] = '\n';
        out[++outp] = '\0';
        break;

    case MODE_OCTAL:
        sim_debug(DEBUG_CARD, dptr, "octal: [");
        out[outp++] = '~';
        for (i = 79; i >= 0; i--) {
            if (image[i] != 0)
               break;
        }
        /* Check if special card */
        if (i == 0) {
            out[outp++] = 'e';
            out[outp++] = 'o';
            if (image[0] == 07) {
               out[outp++] = 'r';
               out[outp++] = '\n';
               sim_debug(DEBUG_CARD, dptr, "eor\n");
               break;
            }
            if (image[0] == 015) {
               out[outp++] = 'f';
               out[outp++] = '\n';
               sim_debug(DEBUG_CARD, dptr, "eof\n");
               break;
            }
            if (image[0] == 017) {
               out[outp++] = 'f';
               out[outp++] = '\n';
               sim_debug(DEBUG_CARD, dptr, "eoi\n");
               break;
            }
        }
        out[outp++] = 'r';
        out[outp++] = 'a';
        out[outp++] = 'w';
        for (i = 0; i < 80; i++) {
            uint16 col = image[i];
            out[outp++] = ((col >> 9) & 07) + '0';
            out[outp++] = ((col >> 6) & 07) + '0';
            out[outp++] = ((col >> 3) & 07) + '0';
            out[outp++] = (col & 07) + '0';
        }
        out[outp++] = '\n';
        sim_debug(DEBUG_CARD, dptr, "%s", &out[4]);
        break;


    case MODE_BIN:
        sim_debug(DEBUG_CARD, dptr, "bin\n");
        for (i = 0; i < 80; i++) {
            uint16      col = image[i];
            out[outp++] = (col & 0x00f) << 4;
            out[outp++] = (col & 0xff0) >> 4;
        }
        break;

    case MODE_CBN:
        sim_debug(DEBUG_CARD, dptr, "cbn\n");
        /* Fill buffer */
        for (i = 0; i < 80; i++) {
            uint16      col = image[i];
            out[outp++] = (col >> 6) & 077;
            out[outp++] = col & 077;
        }
        /* Now set parity */
        for (i = 0; i < 160; i++)
            out[i] |= 0100 ^ sim_parity_table[(int)out[i]];
        out[0] |= 0x80;     /* Set record mark */
        break;

    case MODE_BCD:
        sim_debug(DEBUG_CARD, dptr, "bcd [");
        for (i = 0; i < 80; i++, outp++) {
             out[outp] = sim_hol_to_bcd(image[i]);
             if (out[outp] != 0x7f)
                 out[outp] |= sim_parity_table[(int)out[outp]];
             else
                 out[outp] = 077;
            sim_debug(DEBUG_CARD, dptr, "%c",
                         sim_six_to_ascii[(int)out[outp]]);
        }
        sim_debug(DEBUG_CARD, dptr, "]\n");
        out[0] |= 0x80;     /* Set record mark */
        while (outp > 0 && out[--outp] == 0);
        outp++;
        break;

    case MODE_EBCDIC:
        sim_debug(DEBUG_CARD, dptr, "ebcdic\n");
        /* Fill buffer */
        for (i = 0; i < 80; i++, outp++) {
            uint16      col = image[i];
            out[outp] = 0xff & hol_to_ebcdic[col];
        }
        break;
    }
    data->punch_count++;
    sim_fwrite(out, 1, outp, uptr->fileref);
    uptr->pos = ftell (uptr->fileref);
    /* Clear image buffer */
    for (i = 0; i < 80; image[i++] = 0);
    return CDSE_OK;
}

/* Set card format */
t_stat sim_card_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int f;

    if (uptr == NULL) return SCPE_IERR;
    if (cptr == NULL) return SCPE_ARG;
    for (f = 0; fmts[f].name != 0; f++) {
        if (strcmp (cptr, fmts[f].name) == 0) {
            uptr->flags = (uptr->flags & ~UNIT_CARD_MODE) | fmts[f].mode;
            return SCPE_OK;
            }
        }
    return SCPE_ARG;
}

/* Show card format */

t_stat sim_card_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int f;

    for (f = 0; fmts[f].name != 0; f++) {
        if ((uptr->flags & UNIT_CARD_MODE) == fmts[f].mode) {
            fprintf (st, "%s format", fmts[f].name);
            return SCPE_OK;
        }
    }
    fprintf (st, "invalid format");
    return SCPE_OK;
}


t_stat
sim_card_attach(UNIT * uptr, CONST char *cptr)
{
    t_stat               r = SCPE_OK;
    int                  eof = 0;
    struct card_context *data;
    char                 gbuf[30];
    int                  i;
    char                *saved_filename;
    t_bool              was_attached = (uptr->flags & UNIT_ATT);
    t_addr              saved_pos;
    static int          ebcdic_init = 0;

    if ((uptr->flags & UNIT_RO) &&      /* Attaching a Reader */
            strchr (cptr, ',')) {       /* Restoring Attach list of files? */
        char tbuf[10*CBUFSIZE];
        char *tptr = tbuf;
        int32 saved_switches = sim_switches;

        strlcpy (tbuf, cptr, sizeof(tbuf));
        tptr = strtok (tptr, ",");
        while (tptr) {
            cptr = tptr;
            while (isspace(*cptr))
                ++cptr;
            r = sim_card_attach (uptr, cptr);
            if (r != SCPE_OK)
                return r;
            tptr = strtok (NULL, ",");
            sim_switches = saved_switches & ~SWMASK('F');
            sim_switches |= SWMASK('S');
        }
        sim_switches = saved_switches;
        return SCPE_OK;
    }

    cptr = get_sim_sw (cptr);                               /* Pickup optional format specifier during RESTORE */
    if (sim_switches & SWMASK ('F')) {                      /* format spec? */
        cptr = get_glyph (cptr, gbuf, 0);                   /* get spec */
        if (*cptr == 0) return SCPE_2FARG;                  /* must be more */
        if (sim_card_set_fmt (uptr, 0, gbuf, NULL) != SCPE_OK)
            return SCPE_ARG;
    }

    if (sim_switches & SWMASK ('E'))
       eof = 1;

    saved_filename = uptr->filename;
    uptr->filename = NULL;
    saved_pos = uptr->pos;
    if ((r = attach_unit(uptr, cptr)) != SCPE_OK) {
        uptr->filename = saved_filename;
        uptr->pos = saved_pos;
        return r;
    }


    /* Initialize reverse mapping if not initialized */
    /* Set all to invalid */
    /* Allocate a buffer if one does not exist */
    if (uptr->card_ctx == 0) {
        uptr->card_ctx = malloc(sizeof(struct card_context));
        data = (struct card_context *)uptr->card_ctx;
        memset(data, 0, sizeof(struct card_context));
    } else {
        data = (struct card_context *)uptr->card_ctx;
    }

    if (!ebcdic_init) {
        for (i = 0; i < 4096; i++)
            hol_to_ebcdic[i] = 0x100;
        for (i = 0; i < 256; i++) {
            uint16     temp = ebcdic_to_hol[i];
            if (hol_to_ebcdic[temp] != 0x100) {
                fprintf(stderr, "Translation error %02x is %03x and %03x\n",
                    i, temp, hol_to_ebcdic[temp]);
            } else {
                hol_to_ebcdic[temp] = i;
            }
        }
        ebcdic_init = 1;
    }

    memset(&data->hol_to_ascii[0], 0xff, 4096);
    for(i = 0; i < (sizeof(ascii_to_hol_026)/sizeof(uint16)); i++) {
         uint16          temp;
         switch(uptr->flags & MODE_CHAR) {
         default:
         case 0:
         case MODE_026:
              temp = ascii_to_hol_026[i];
              break;
         case MODE_029:
              temp = ascii_to_hol_029[i];
              break;
         }
         if ((temp & 0xf000) == 0) {
            data->hol_to_ascii[temp] = i;
         }
    }

    if (uptr->flags & UNIT_RO) {            /* Card Reader? */
        t_addr  previous_cards = data->hopper_cards;

        /* Check if we should append to end of existing */
        if ((sim_switches & SWMASK ('S')) == 0) {
           previous_cards = 0;
           data->hopper_cards = 0;
           data->hopper_size = 0;
           data->punch_count = 0;
           free(data->images);
           data->images = NULL;
           free(saved_filename);
           saved_filename = NULL;
           saved_pos = 0;
        }

        /* Go read the deck */
        r = _sim_read_deck(uptr, eof);
        uptr->pos = saved_pos;
        detach_unit(uptr);
        if (was_attached) {
            uptr->flags |= UNIT_ATT;
            uptr->filename = saved_filename;
        }
        if (r == SCPE_OK) {
            const char    *fmt = "AUTO";
            int            mode = uptr->flags & UNIT_CARD_MODE;
            for (i = 0; fmts[i].name != 0; i++) {
                if (fmts[i].mode == mode) {
                    fmt = fmts[i].name;
                    break;
                }
            }
            uptr->flags |= UNIT_ATT;
            uptr->dynflags |= UNIT_ATTMULT;
            if (saved_filename) {
                uptr->filename = (char *)malloc (32 + strlen (cptr) + strlen (saved_filename));
                sprintf (uptr->filename, "%s, %s-F %s %s", saved_filename,
                     (eof)? "-E ": "", fmt, cptr);
                free(saved_filename);
            } else {
                uptr->filename = (char *)malloc (32 + strlen (cptr));
                sprintf (uptr->filename, "%s-F %s %s", (eof)?"-E ": "", fmt, cptr);
            }
            r = sim_messagef(SCPE_OK, "%s: %d card Deck Loaded from %s\n",
                       sim_uname(uptr), data->hopper_cards - previous_cards, cptr);
        } else {
            if (uptr->dynflags & UNIT_ATTMULT)
                uptr->flags |= UNIT_ATT;
            detach_unit(uptr);
            return r;
        }
    }

    return r;
}

t_stat
sim_card_detach(UNIT * uptr)
{
    /* Free buffer if one allocated */
    if (uptr->card_ctx != 0) {
        struct card_context * data = (struct card_context *)uptr->card_ctx;
        /* No clear any existing decks on stack */
        free(data->images);
        free(uptr->card_ctx);
        uptr->card_ctx = 0;
    }
    if (uptr->flags & UNIT_RO) {           /* Card Reader? */
       free(uptr->filename);
       uptr->filename = NULL;
       uptr->flags &= ~UNIT_ATT;
       uptr->dynflags &= ~UNIT_ATTMULT;
       return SCPE_OK;                     /* Already detached */
    }
    return detach_unit(uptr);
}

t_stat sim_card_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "%s Card %sAttach Help\n\n", dptr->name, (uptr->flags & UNIT_RO) ? "Reader " : "Punch ");
    if (0 == (uptr-dptr->units)) {
        if (dptr->numunits > 1) {
            uint32 i;

            for (i=0; i < dptr->numunits; ++i)
                if (dptr->units[i].flags & UNIT_ATTABLE)
                    fprintf (st, "  sim> ATTACH {switches} %s%d carddeck\n\n", dptr->name, i);
            }
        else
            fprintf (st, "  sim> ATTACH {switches} %s carddeck\n\n", dptr->name);
        }
    else
        fprintf (st, "  sim> ATTACH {switches} %s carddeck\n\n", dptr->name);
    fprintf (st, "Attach command switches\n");
    fprintf (st, "    -F          Open the indicated card deck in a specific format (default\n");
    fprintf (st, "                is AUTO, alternatives are BIN, TEXT, BCD and CBN)\n");
    if ((uptr->flags & UNIT_RO) == 0) {
        fprintf (st, "    -N          Create a new punch output file (default is to append to\n");
        fprintf (st, "                an existing file if it exists)\n");
    } else {
        fprintf (st, "    -E          Return EOF after deck read\n");
        fprintf (st, "    -S          Append deck to cards currently waiting to be read\n");
    }
    return SCPE_OK;
}

static t_stat create_card_file (const char *filename, int cards)
{
FILE *f;
int i;

f = fopen (filename, "w");
if (f == NULL)
    return SCPE_OPENERR;
for (i=0; i<cards; i++)
    fprintf (f, "%05d ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\n", i);
fclose (f);
return SCPE_OK;
}

#include <setjmp.h>

t_stat sim_card_test (DEVICE *dptr)
{
t_stat stat = SCPE_OK;
#if defined(USE_SIM_CARD) && defined(SIM_CARD_API)
char cmd[CBUFSIZE];
char saved_filename[4*CBUFSIZE];
uint16 card_image[80];
SIM_TEST_INIT;

if ((dptr->units->flags & UNIT_RO) == 0)  /* Punch device? */
    return SCPE_OK;

sim_printf ("Testing %s device sim_card APIs\n", dptr->name);

(void)remove("file1.deck");
(void)remove("file2.deck");
(void)remove("file3.deck");
(void)remove("file4.deck");

SIM_TEST(create_card_file ("File10.deck", 10));
SIM_TEST(create_card_file ("File20.deck", 20));
SIM_TEST(create_card_file ("File30.deck", 30));
SIM_TEST(create_card_file ("File40.deck", 40));

sprintf (cmd, "%s File10.deck", dptr->name);
SIM_TEST(attach_cmd (0, cmd));
sprintf (cmd, "%s File20.deck", dptr->name);
SIM_TEST(attach_cmd (0, cmd));
sprintf (cmd, "%s -S File30.deck", dptr->name);
SIM_TEST(attach_cmd (0, cmd));
sprintf (cmd, "%s -S -E File40.deck", dptr->name);
SIM_TEST(attach_cmd (0, cmd));
sprintf (saved_filename, "%s %s", dptr->name, dptr->units->filename);
show_cmd (0, dptr->name);
sim_printf ("Input Hopper Count:  %d\n", sim_card_input_hopper_count(dptr->units));
sim_printf ("Output Hopper Count: %d\n", sim_card_output_hopper_count(dptr->units));
while (!sim_card_eof (dptr->units))
    SIM_TEST(sim_read_card (dptr->units, card_image));
sim_printf ("Input Hopper Count:  %d\n", sim_card_input_hopper_count(dptr->units));
sim_printf ("Output Hopper Count: %d\n", sim_card_output_hopper_count(dptr->units));
sim_printf ("Detaching %s\n", dptr->name);
SIM_TEST(detach_cmd (0, dptr->name));
show_cmd (0, dptr->name);
sim_printf ("Input Hopper Count:  %d\n", sim_card_input_hopper_count(dptr->units));
sim_printf ("Output Hopper Count: %d\n", sim_card_output_hopper_count(dptr->units));
sim_printf ("Attaching Saved Filenames: %s\n", saved_filename + strlen(dptr->name));
SIM_TEST(attach_cmd (0, saved_filename));
show_cmd (0, dptr->name);
sim_printf ("Input Hopper Count:  %d\n", sim_card_input_hopper_count(dptr->units));
sim_printf ("Output Hopper Count: %d\n", sim_card_output_hopper_count(dptr->units));
SIM_TEST(detach_cmd (0, dptr->name));
(void)remove ("file10.deck");
(void)remove ("file20.deck");
(void)remove ("file30.deck");
(void)remove ("file40.deck");
#endif /* defined(USE_SIM_CARD) && defined(SIM_CARD_API) */
return stat;
}

#else   /* !defined(USE_SIM_CARD) */

t_stat sim_card_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    return SCPE_OK;
}

t_stat sim_card_test (DEVICE *dptr)
{
    return SCPE_OK;
}

#endif /* USE_SIM_CARD */
