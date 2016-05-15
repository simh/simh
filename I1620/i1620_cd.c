/* i1620_cd.c: IBM 1622 card reader/punch

   Copyright (c) 2002-2015, Robert M. Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   cdr          1622 card reader
   cdp          1622 card punch

   31-Jan-15    TFM     Changes to translation tables (Tom McBride)
   10-Dec-13    RMS     Fixed WA card punch translations (Bob Armstrong)
                        Fixed card reader EOL processing (Bob Armstrong)
   19-Mar-12    RMS     Fixed declarations of saved_pc, io_stop (Mark Pizzolato)
   19-Jan-07    RMS     Set UNIT_TEXT flag
   13-Jul-06    RMS     Fixed card reader fgets call (Tom McBride)
                        Fixed card reader boot sequence (Tom McBride)
   21-Sep-05    RMS     Revised translation tables for 7094/1401 compatibility
   25-Apr-03    RMS     Revised for extended file support

   Cards are represented as ASCII text streams terminated by newlines.
   This allows cards to be created and edited as normal files.
*/

#include "i1620_defs.h"

#define CD_LEN          80

extern uint8 M[MAXMEMSIZE];
extern uint8 ind[NUM_IND];
extern UNIT cpu_unit;
extern uint32 io_stop;

char cdr_buf[CD_LEN + 2];
char cdp_buf[CD_LEN + 2];

t_stat cdr_reset (DEVICE *dptr);
t_stat cdr_attach (UNIT *uptr, CONST char *cptr);
t_stat cdr_boot (int32 unitno, DEVICE *dptr);
t_stat cdr_read (void);
t_stat cdp_reset (DEVICE *dptr);
t_stat cdp_write (uint32 len);
t_stat cdp_num (uint32 pa, uint32 ndig, t_bool dump);

/* Card reader data structures

   cdr_dev      CDR descriptor
   cdr_unit     CDR unit descriptor
   cdr_reg      CDR register list
*/

UNIT cdr_unit = {
    UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE+UNIT_TEXT, 0)
    };

REG cdr_reg[] = {
    { FLDATA (LAST, ind[IN_LAST], 0) },
    { DRDATA (POS, cdr_unit.pos, T_ADDR_W), PV_LEFT },
    { NULL }
    };

DEVICE cdr_dev = {
    "CDR", &cdr_unit, cdr_reg, NULL,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &cdr_reset,
    &cdr_boot, &cdr_attach, NULL
    };

/* CDP data structures

   cdp_dev      CDP device descriptor
   cdp_unit     CDP unit descriptor
   cdp_reg      CDP register list
*/

UNIT cdp_unit = {
    UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0)
    };

REG cdp_reg[] = {
    { DRDATA (POS, cdp_unit.pos, T_ADDR_W), PV_LEFT },
    { NULL }
    };

DEVICE cdp_dev = {
    "CDP", &cdp_unit, cdp_reg, NULL,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &cdp_reset,
    NULL, NULL, NULL
    };

/* Data tables.  The card reader presents unusual problems.

   Some of these translations may seem strange. The 1620 could
   read and punch cards numerically (one 1620 storage location
   per card column) or alphabetically (two 1620 storage locations
   per card column). Even though a card might have contained any
   possible character (digit, letter, special character), it 
   could still be read numerically. In this case, some characters 
   behaved the same as numbers or as record marks. The results 
   are well defined in IBM documentation. 

   In order to make it possible to prepare card decks for input
   using normal text editors, ASCII characters have been assigned 
   to represent 1620 characters that could appear on cards. In most
   cases, this was easy since the letters, digits and punctuation
   characters all have equivalent ASCII assignments. Five 1620 
   characters do not have equivalent ASCII graphics and are 
   assigned as follows:

     ]  is used to represent a flagged zero
     |  is used to represent a record mark
     !  is used to represent a flagged record mark
     }  is used to represent a group mark
     "  is used to represent a flagged group mark

   As a concession to some editors, ASCII nul, nl, tab, and lf 
   characters are accepted and converted to blanks. Also, for
   the same reason, lower case letters are treated the same as
   upper case on input. All other ASCII characters not in the
   1620 character set are treated as errors.

                                                (Tom McBride)
   
*/
/* Card reader (ASCII) to numeric (one digit) */

const int8 cdr_to_num[128] = {
 0x00,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 00 */
   -1, 0x00, 0x00,   -1,   -1, 0x00,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 10 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
 0x00, 0x1A, 0x1F,   -1, 0x1B,   -1,   -1,   -1,        /*  !" $    */
 0x0C, 0x0C, 0x1C, 0x00, 0x0B, 0x10, 0x0B, 0x01,        /* ()*+,-./ */
 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,        /* 01234567 */
 0x08, 0x09,   -1,   -1,   -1, 0x0B,   -1,   -1,        /* 89   =   */
 0x0C, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,        /* @ABCDEFG */
 0x08, 0x09, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,        /* HIJKLMNO */
 0x17, 0x18, 0x19, 0x02, 0x03, 0x04, 0x05, 0x06,        /* PQRSTUVW */
 0x07, 0x08, 0x09,   -1,   -1, 0x10,   -1,   -1,        /* XYZ  ]   */
   -1, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,        /* `abcdefg */
 0x08, 0x09, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,        /* hijklmno */
 0x17, 0x18, 0x19, 0x02, 0x03, 0x04, 0x05, 0x06,        /* pqrstuvw */
 0x07, 0x08, 0x09,   -1, 0x0A, 0x0F,   -1,   -1         /* xyz |}   */
 };

/* Numeric (flag + digit) to card punch (ASCII) */

/* Note that all valid digits produce different 
   codes except that both numeric blanks and flagged 
   numeric blanks both produce a blank column. (tfm) */

const int8 num_to_cdp[32] = {
 '0', '1', '2', '3', '4', '5', '6', '7',                /* 0 */
 '8', '9', '|',  -1, ' ',  -1,  -1, '}',
 ']', 'J', 'K', 'L', 'M', 'N', 'O', 'P',                /* F + 0 */
 'Q', 'R', '!',  -1, ' ',  -1,  -1, '"'
 };

/* Card reader (ASCII) to alphameric (two digits)

   ] reads as 50 (flagged zero)
   | reads as 0A (record mark)
   ! reads as 5A (flagged record mark)
   } reads as 0F (group mark)
   " reads as 5F (flagged group mark)

   As a concession to some editors, ASCII nul, nl, tab, and lf 
   characters are accepted and converted to blanks. Also, for
   the same reason, lower case letters are treated the same as
   upper case on input. All other ASCII characters not in the
   1620 character set are treated as errors.  
   
*/

const int8 cdr_to_alp[128] = {
 0x00,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 00 */
   -1, 0x00, 0x00,   -1,   -1, 0x00,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 10 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
 0x00, 0x5A, 0x5F,   -1, 0x13,   -1,   -1,   -1,        /*  !" $    */
 0x24, 0x04, 0x14, 0x10, 0x23, 0x20, 0x03, 0x21,        /* ()*+,-./ */
 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,        /* 01234567 */
 0x78, 0x79,   -1,   -1,   -1, 0x33,   -1,   -1,        /* 89   =   */
 0x34, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /* @ABCDEFG */
 0x48, 0x49, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56,        /* HIJKLMNO */
 0x57, 0x58, 0x59, 0x62, 0x63, 0x64, 0x65, 0x66,        /* PQRSTUVW */
 0x67, 0x68, 0x69,   -1,   -1, 0x50,   -1,   -1,        /* XYZ  ]   */
   -1, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /*  abcdefg */
 0x48, 0x49, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56,        /* hijklmno */
 0x57, 0x58, 0x59, 0x62, 0x63, 0x64, 0x65, 0x66,        /* pqrstuvw */
 0x67, 0x68, 0x69,   -1, 0x0A, 0x0F,   -1,   -1         /* xyz |}   */
 };

/* Alphameric (two digits) to card punch (ASCII).  

   All 1620 compilers are know to punch numeric data
   (i.e. data that it knows will be read back using RNCD)
   in alphameric mode with WACD. Because of that, there are some
   alpha to ASCII translations that absolutely MUST work out right
   or otherwise we won't be able to load the object decks.

     50 - punches as ] (flagged zero) 
     0A - punches as | (record mark)
     0F - punches as } (group mark)

   If a program punches alphameric data that includes a flagged 
   record mark or flagged group mark, they will be punched 
   as below. No known IBM compiler punches any of these but
   some application programs do. The mapping of characters for
   the card reader and punch is such that a card deck can be 
   duplicated with a RACD, WACD loop. 

     5A - punches as ! (flagged record mark)
     5F - punches as " (flagged group mark)
*/

const int8 alp_to_cdp[256] = {
 ' ',  -1,  -1, '.', ')',  -1,  -1,  -1,                /* 00 */
  -1,  -1, '|',  -1,  -1,  -1,  -1, '}',
 '+',  -1,  -1, '$', '*',  -1,  -1,  -1,                /* 10 */ 
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
 '-', '/',  -1, ',', '(',  -1,  -1,  -1,                /* 20 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1, '=', '@',  -1,  -1,  -1,                /* 30 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1, 'A', 'B', 'C', 'D', 'E', 'F', 'G',                /* 40 */
 'H', 'I',  -1,  -1,  -1,  -1,  -1,  -1,
 ']', 'J', 'K', 'L', 'M', 'N', 'O', 'P',                /* 50 */
 'Q', 'R', '!',  -1,  -1,  -1,  -1, '"',
  -1, '/', 'S', 'T', 'U', 'V', 'W', 'X',                /* 60 */
 'Y', 'Z',  -1,  -1,  -1,  -1,  -1,  -1,
 '0', '1', '2', '3', '4', '5', '6', '7',                /* 70 */
 '8', '9',  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* 80 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* 90 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* A0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* B0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* C0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* D0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* E0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* F0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1
 }; 

/* Card reader IO routine
 
   - Hard errors stop the operation and halt the system.
   - Invalid characters place a blank in memory and set RDCHK.
     If IO stop is set, the system halts at the end of the operation.
*/

t_stat cdr (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
int32 i;
int8 cdc;
t_stat r, sta;

sta = SCPE_OK;                                          /* assume ok */
switch (op) {                                           /* case on op */

    case OP_RN:                                         /* read numeric */
        r = cdr_read ();                                /* fill reader buf */
        if (r != SCPE_OK)                               /* error? */
            return r;
        for (i = 0; i < CD_LEN; i++) {                  /* transfer to mem */
            cdc = cdr_to_num[cdr_buf[i]];               /* translate */
            if (cdc < 0) {                              /* invalid? */
                ind[IN_RDCHK] = 1;                      /* set read check */
                if (io_stop)                            /* set return status */
                    sta = STOP_INVCHR;
                cdc = 0;
                }
            M[pa] = cdc;                                /* store digit */
            PP (pa);                                    /* incr mem addr */
            }
        break;

    case OP_RA:                                         /* read alphameric */
        r = cdr_read ();                                /* fill reader buf */
        if (r != SCPE_OK)                               /* error? */
            return r;
        for (i = 0; i < CD_LEN; i++) {                  /* transfer to mem */
            cdc = cdr_to_alp[cdr_buf[i]];               /* translate */
            if (cdc < 0) {                              /* invalid? */
                ind[IN_RDCHK] = 1;                      /* set read check */
                if (io_stop)                            /* set return status */
                    sta = STOP_INVCHR;
                cdc = 0;
                };
            M[pa] = (M[pa] & FLAG) | (cdc & DIGIT);     /* store 2 digits */
            M[pa - 1] = (M[pa - 1] & FLAG) | ((cdc >> 4) & DIGIT);
            pa = ADDR_A (pa, 2);                        /* incr mem addr */
            }
        break;  

    default:                                            /* invalid function */
        return STOP_INVFNC;
        }

return sta;
}

/* Fill card reader buffer - all errors are hard errors

   As Bob Armstrong pointed out, this routines needs to account
   for variants in text file formats, which may terminate lines
   with cr-lf (Windows), lf (UNIX), or cr (Mac).
*/

t_stat cdr_read (void)
{
int32 i;

ind[IN_LAST] = 0;                                       /* clear last card */
if ((cdr_unit.flags & UNIT_ATT) == 0) {                 /* attached? */
    ind[IN_RDCHK] = 1;                                  /* no, error */
    return SCPE_UNATT;
    }

for (i = 0; i < CD_LEN + 2; i++)                        /* clear buffer */
    cdr_buf[i] = ' ';
fgets (cdr_buf, CD_LEN + 2, cdr_unit.fileref);          /* read card */
if (feof (cdr_unit.fileref))                            /* eof? */
    return STOP_NOCD;
if (ferror (cdr_unit.fileref)) {                        /* error? */
    ind[IN_RDCHK] = 1;                                  /* set read check */
    sim_perror ("CDR I/O error");
    clearerr (cdr_unit.fileref);
    return SCPE_IOERR;
    }
if ((i = strlen (cdr_buf)) > 0) {                       /* anything at all? */
    if (cdr_buf[i-1] == '\n') {                         /* line end in \n? */
        cdr_buf[i-1] = 0;                               /* remove it */
        }
    else if (cdr_buf[i-1] == '\r') {                    /* line end in \r? */
        cdr_buf[i-1] = 0;                               /* remove it */
        cdr_unit.pos = ftell (cdr_unit.fileref);        /* save position */
        if (fgetc (cdr_unit.fileref) != '\n')           /* next char not \n? */
            fseek (cdr_unit.fileref, cdr_unit.pos, SEEK_SET); /* then rewind */  
        }
    else {                                              /* line too long */
        ind[IN_RDCHK] = 1;
        sim_printf ("CDR line too long");
        return SCPE_IOERR;
        }
    }
cdr_unit.pos = ftell (cdr_unit.fileref);                /* update position */
getc (cdr_unit.fileref);                                /* see if more */
if (feof (cdr_unit.fileref))                            /* eof? set last */
    ind[IN_LAST] = 1;
fseek (cdr_unit.fileref, cdr_unit.pos, SEEK_SET);       /* "backspace" */
return SCPE_OK;
}

/* Card reader attach */

t_stat cdr_attach (UNIT *uptr, CONST char *cptr)
{
ind[IN_LAST] = 0;                                       /* clear last card */
return attach_unit (uptr, cptr);
}

/* Card reader reset */

t_stat cdr_reset (DEVICE *dptr)
{
ind[IN_LAST] = 0;                                       /* clear last card */
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START      0

t_stat cdr_boot (int32 unitno, DEVICE *dptr)
{
t_stat r;
uint32 old_io_stop;
extern uint32 saved_PC;

old_io_stop = io_stop;
io_stop = 1;
r = cdr (OP_RN, 0, 0, 0);                               /* read card @ 0 */
io_stop = old_io_stop;
if (r != SCPE_OK)                                       /* error? */
    return r;
saved_PC = BOOT_START;
return SCPE_OK;
}

/* Card punch IO routine 

   - Hard errors stop the operation and halt the system.
   - Invalid characters stop the operation and set WRCHK.
     If IO stop is set, the system halts.
*/

t_stat cdp (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
int32 i;
int8 cdc;
uint8 z, d;

switch (op) {                                           /* decode op */

    case OP_DN:

        /* DN punches all characters the same as WN except that a flagged
           zero is punched as a hypehen (-) instead of a flagged
           zero ([). Punching begins at the P address and continues until
           the last digit of the storage module containing the P address
           has been punched. If the amount of data to be punched is an 
           exact multiple of 80, the operation ends there. If the last
           character of the module does not fill out the card, additional
           characters from the next higher addresses (addressing wraps to
           back to zero if the operation started in the highest module) are 
           used to fill out the card.                   (Tom McBride) */

        return cdp_num (pa,                             /* dump numeric */
                        ((20000 - (pa % 20000) + 79) / 80) * 80,
                        TRUE);

    case OP_WN:

        /* WN always punches exactly 80 characters. If the highest address
           in the machine is reached before the card is full, addressing 
           wraps around to zero and continues. The PP function handles
           this correctly.                              (Tom McBride) */

        return cdp_num (pa, CD_LEN, FALSE);             /* write numeric */

    case OP_WA:                                         /* write alphanumerically */

        /* WA always punches exactly 80 characters. If the highest address
           in the machine is reached before the card is full, addressing
           wraps around to zero and continues. The ADDR_A function handles
           this correctly.                              (Tom McBride) */

        for (i = 0; i < CD_LEN; i++) {                  /* one card */
            d = M[pa] & DIGIT;                          /* get digit pair */
            z = M[pa - 1] & DIGIT;
            cdc = alp_to_cdp[(z << 4) | d];             /* translate */
            if (cdc < 0) {                              /* bad char? */
                ind[IN_WRCHK] = 1;                      /* set write check */
                CRETIOE (io_stop, STOP_INVCHR);
                }
            cdp_buf[i] = cdc;                           /* store in buf */
            pa = ADDR_A (pa, 2);                        /* incr mem addr */
            }
        return cdp_write (CD_LEN);                      /* punch buffer */

    default:                                            /* invalid function */
        break;
        }

return STOP_INVFNC;
}

/* Punch card numeric */

t_stat cdp_num (uint32 pa, uint32 ndig, t_bool dump)
{
int32 i, ncd, len;
uint8 d;
int8 cdc;
t_stat r;

ncd = ndig / CD_LEN;                                    /* number of cards */
while (ncd-- >= 0) {                                    /* until done */
    len = (ncd >= 0)? CD_LEN: (ndig % CD_LEN);          /* card length */
    if (len == 0)
        break;
    for (i = 0; i < len; i++) {                         /* one card */
        d = M[pa] & (FLAG | DIGIT);                     /* get char */
        if (dump && (d == FLAG))                        /* dump? F+0 is diff .. */
            cdc = '-';                                  /* .. punch as hyphen */
        else cdc = num_to_cdp[d];                       /* translate */
        if (cdc < 0) {                                  /* bad char? */
            ind[IN_WRCHK] = 1;                          /* set write check */
            CRETIOE (io_stop, STOP_INVCHR);             /* stop */
            }
        cdp_buf[i] = cdc;                               /* store in buf */
        PP (pa);                                        /* incr mem addr */
        }
    r = cdp_write (len);                                /* punch card */
    if (r != SCPE_OK)                                   /* error? */
        return r;
    }
return SCPE_OK;
}

/* Write punch card buffer - all errors are hard errors */

t_stat cdp_write (uint32 len)
{
if ((cdp_unit.flags & UNIT_ATT) == 0) {                 /* attached? */
    ind[IN_WRCHK] = 1;                                  /* no, error */
    return SCPE_UNATT;
    }

while ((len > 0) && (cdp_buf[len - 1] == ' '))          /* trim spaces */
    --len;
cdp_buf[len] = '\n';                                    /* newline, null */
cdp_buf[len + 1] = 0;

fputs (cdp_buf, cdp_unit.fileref);                      /* write card */
cdp_unit.pos = ftell (cdp_unit.fileref);                /* count char */
if (ferror (cdp_unit.fileref)) {                        /* error? */
    ind[IN_WRCHK] = 1;
    sim_perror ("CDR I/O error");
    clearerr (cdp_unit.fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Reset card punch */

t_stat cdp_reset (DEVICE *dptr)
{
return SCPE_OK;
}
