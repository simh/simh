/*
 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to sim@ibm1130.org
 */

/*
 * 03 ctrl-C         => Program stop      (not handled here)
 * 05 ctrl-E         => Simulator stop    (not handled here)
 * 08 ctrl-H         => Backspace
 * 0D ctrl-M (Enter) => EOF
 * 11 ctrl-Q         => Interrupt request (not handled here)
 * 12 ctrl-R         => "cent" (R because that's where cent is on the 1130 keyboard)
 * 15 ctrl-U         => Erase Field
 * 7E ~              => "not"
 * FF Del            => Backspace again
 */

static uint16 ascii_to_conin[] =            /* ASCII to ((hollerith << 4) | special key flags)    */
{
              /*  00     01     02     03     04     05     06     07     08     09     0A     0B     0C     0D     0E     0F */
    /* 00 */       0,     0,     0,     0,     0,     0,     0,     0,0x0004,     0,     0,     0,     0,0x0008,     0,     0,
    /* 10 */       0,     0,0x8820,     0,     0,0x0002,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    /* 20 */  0x0001,0x4820,0x0060,0x0420,0x4420,0x2220,0x8000,0x0120,0x8120,0x4120,0x4220,0x80a0,0x2420,0x4000,0x8420,0x3000,
    /* 30 */  0x2000,0x1000,0x0800,0x0400,0x0200,0x0100,0x0080,0x0040,0x0020,0x0010,0x0820,0x40a0,0x8220,0x00a0,0x20a0,0x2060,
    /* 40 */  0x0220,0x9000,0x8800,0x8400,0x8200,0x8100,0x8080,0x8040,0x8020,0x8010,0x5000,0x4800,0x4400,0x4200,0x4100,0x4080,
    /* 50 */  0x4040,0x4020,0x4010,0x2800,0x2400,0x2200,0x2100,0x2080,0x2040,0x2020,0x2010,     0,     0,     0,     0,0x2120,
    /* 60 */       0,0x9000,0x8800,0x8400,0x8200,0x8100,0x8080,0x8040,0x8020,0x8010,0x5000,0x4800,0x4400,0x4200,0x4100,0x4080,
    /* 70 */  0x4040,0x4020,0x4010,0x2800,0x2400,0x2200,0x2100,0x2080,0x2040,0x2020,0x2010,     0,0xB060,     0,     0,0x0004,
    /* 80 */       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    /* 90 */       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    /* a0 */       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    /* b0 */       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    /* c0 */       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    /* d0 */       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    /* e0 */       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    /* f0 */       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,0x0004,
};
