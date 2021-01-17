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
                First character   21012345
                                  111
                Second characters 6789----
                Top 4 bits of second character are 0.
                It is unlikely that ascii text or BCD format
                text could produce similar profile.

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

    For autodetection of BCD card format, there can be no parity errors.
    All undeterminate formats are treated as ASCII.

    ASCII mode recognizes some additional forms of input which allows the
    intermixing of binary cards with text cards. 

    Lines beginning with ~raw are taken as a number of 4 digit octal values
    with represent each column of the card from 12 row down to 9 row. If there
    is not enough octal numbers to span a full card the remainder of the 
    card will not be punched.

    Also ~eor, will generate a 7/8/9 punch card. An ~eof will gernerate a
    6/7/9 punch card, and a ~eoi will generate a 6/7/8/9 punch.

    A single line of ~ will set the EOF flag when that card is read.

    Auto output format is ASCII if card has only printable characters
    or card format binary.
*/

#ifndef SIM_CARD_H_
#define SIM_CARD_H_     0

#ifdef  __cplusplus
extern "C" {
#endif

#define SIM_CARD_API    2               /* API Version */

#define DEBUG_CARD      0x80000000      /* Show details */

/* Flags for punch and reader. */
#define UNIT_V_CARD_MODE  (UNIT_V_UF + 0)
#define UNIT_CARD_MODE  (7 << UNIT_V_CARD_MODE)
#define MODE_AUTO       (0 << UNIT_V_CARD_MODE)
#define MODE_BIN        (1 << UNIT_V_CARD_MODE)
#define MODE_TEXT       (2 << UNIT_V_CARD_MODE)
#define MODE_BCD        (3 << UNIT_V_CARD_MODE)
#define MODE_CBN        (4 << UNIT_V_CARD_MODE)
#define MODE_EBCDIC     (5 << UNIT_V_CARD_MODE)
#define MODE_OCTAL      (6 << UNIT_V_CARD_MODE)
/* Allow lower case letters */
#define MODE_LOWER      (8 << UNIT_V_CARD_MODE)
#define MODE_026        (0x10 << UNIT_V_CARD_MODE)
#define MODE_029        (0x20 << UNIT_V_CARD_MODE)
#define MODE_DEC29      (0x30 << UNIT_V_CARD_MODE)
#define MODE_CHAR       (0x70 << UNIT_V_CARD_MODE)


/* Card Reader Return Status code */
typedef int t_cdstat;
#define CDSE_OK     0   /* Good */
#define CDSE_EOF    1   /* End of File */
#define CDSE_EMPTY  2   /* Input Hopper Empty */
#define CDSE_ERROR  3   /* Error Card Read */  

/* Generic routines. */

     /* Read next card into image row 12,11,10,1-9 */
     /* Return SCPE_EOF if end file detected. */
t_cdstat sim_read_card(UNIT * uptr, uint16 image[80]);
     /* Punch card from image row 12,11,10,1-9 */
t_cdstat sim_punch_card(UNIT * uptr, uint16 image[80]);
     /* Check if next card to be read is EOF */
int      sim_card_eof(UNIT * uptr);
     /* Return number of cards yet to read */
t_addr   sim_hopper_size(UNIT * uptr);
     /* Return number of cards punched */
t_addr   sim_punch_count(UNIT * uptr);
t_addr   sim_card_input_hopper_count(UNIT *uptr);
t_addr   sim_card_output_hopper_count(UNIT *uptr);
t_stat   sim_card_attach(UNIT * uptr, CONST char *file);
t_stat   sim_card_detach(UNIT *uptr);

/* Conversion routines to save code */
uint16   sim_bcd_to_hol(uint8 bcd);
uint16   sim_ebcdic_to_hol(uint8 ebcdic);
uint8    sim_hol_to_bcd(uint16 hol);
uint16   sim_hol_to_ebcdic(uint16 hol);

/* Format control routines. */
t_stat   sim_card_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat   sim_card_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* Help information */
t_stat   sim_card_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

/* Translation tables */
extern const char      sim_six_to_ascii[64];        /* Map BCD to ASCII */
extern const char      sim_ascii_to_six[128];       /* Map 7 bit ASCII to BCD */
extern const uint8     sim_parity_table[64];        /* 64 entry odd parity table */

/* Unit test routine */
extern t_stat sim_card_test (DEVICE *dptr);

#ifdef  __cplusplus
}
#endif

#endif
