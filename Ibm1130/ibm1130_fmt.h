/*
 * (C) Copyright 2003, Bob Flander.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to bob@jftr.com
 */

/* ibm1130_asm.h: definition of routines in ibm1130_asm.c
 */

char*	EditToAsm(char*);							/* convert edit format to 1130 assembler format */
char*	EditToFortran(char*);						/* convert edit format to Fortran format */
char*	EditToWhitespace(char*);					/* clean white space, tabstops every 8 positions */
