/* i650_cdr.c: IBM 650 Card reader.

   Copyright (c) 2018, Roberto Sancho

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
   ROBERTO SANCHO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This is the standard card reader.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "i650_defs.h"
#include "sim_card.h"

#define UNIT_CDR        UNIT_ATTABLE | UNIT_RO | MODE_026


/* std devices. data structures

   cdr_dev      Card Reader device descriptor
   cdr_unit     Card Reader unit descriptor
   cdr_reg      Card Reader register list
   cdr_mod      Card Reader modifiers list
*/

uint32              cdr_cmd(UNIT *, uint16, uint16);
t_stat              cdr_srv(UNIT *);
t_stat              cdr_reset(DEVICE *);
t_stat              cdr_attach(UNIT *, CONST char *);
t_stat              cdr_detach(UNIT *);
t_stat              cdr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdr_description(DEVICE *dptr);
t_stat              cdr_set_wiring (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat              cdr_show_wiring (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

UNIT                cdr_unit[] = {
   {UDATA(cdr_srv, UNIT_CDR, 0), 300},  // 4 readers. Unit 0 not used
   {UDATA(cdr_srv, UNIT_CDR, 0), 300},       
   {UDATA(cdr_srv, UNIT_CDR, 0), 300},       
   {UDATA(cdr_srv, UNIT_CDR, 0), 300},       
};

MTAB                cdr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT", &sim_card_set_fmt, &sim_card_show_fmt, NULL, "Set card format"},
    {MTAB_XTD | MTAB_VUN, 0, "WIRING", "WIRING", &cdr_set_wiring, &cdr_show_wiring, NULL, "Set card read control panel Wiring"},
    {0}
};

DEVICE              cdr_dev = {
    "CDR", cdr_unit, NULL, cdr_mod,
    4, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cdr_attach, &sim_card_detach,
    &cdr_dib, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &cdr_help, NULL, NULL, &cdr_description
};

static struct card_wirings wirings[] = {
    {WIRING_8WORD,  "8WORD"},
    {WIRING_SOAP,   "SOAP"}, 
    {WIRING_IS,     "IS"}, 
    {0, 0},
};


// decode digit 0-9 read from card to get value and X(11) and Y(12) punch state (minus/HiPunch)
// return -1 if not a digit number
int decode_digit(char c1, int * HiPunch, int * NegPunch)
{
    int i,n; 

    *HiPunch = *NegPunch = 0;
    //       N is 0..9 or ?A..I (0..9 with Y(12) High Punch set)
    //                 or !J..R (0..9 with X(11) Minus Punch set). 
    //                 or &S..Z# (0..9 with both X(11) and Y(12) Punch set). 

    if (c1 == 32)  return 0;                    // space read as zero
    for (i=0; i<40; i++) {
        if (c1 == digits_ascii[i]) {
            n = i % 10;
            i = i / 10;
            *HiPunch  = (i & 1);
            *NegPunch = (i >> 1);
            return n;
        }
    }
    return -1;                                  // not a valid digit
}

// get 10 digits word from buf, with sign. return 1 if HiPunch set on any digit
int decode_8word_wiring(char * buf, int addr) 
{
    // decode up to 8 numerical words per card 
    // input card
    //       NNNNNNNNNN ... 8 times
    //       N is 0..9 or ?A..I (0..9 with Y(12) High Punch set)
    //                 or !J..R (0..9 with X(11) Minus Punch set). 
    //                 or &S..Z# (0..9 with both X(11) and Y(12) Punch set). 
    //       If last digit of word has X(11) punch whole word is set as negative value
    //       If N is a space, a 0 is assumed
    // put the decoded data in drum at addr (if addr < 0 -> do not store in drum)
    // return 1 if any colum has Y(12) hi-punch set
    int c1,c2,wn,eor,iCol;
    int HiPunch, hip; 
    int NegPunch, NegZero; 
    int nDigits; 
    t_int64 d;

    NegZero = 0;                            // flag set if negative zero is read
    HiPunch = 0;                            // set to 1 if Y(12) high punch found
    eor = 0;                                // signals end of card record
    iCol = 0;                               // current read colum in card
    for (wn=0;wn<8;wn++) {                  // one card generates 8 words in drum mem
        d = 0;
        nDigits=0;                          // number of digits
        while (1) {
            c1 = buf[iCol++];
            if (c1 < ' ') {eor = 1; break;} // end of card
            c2 = decode_digit(c1, &hip, &NegPunch);
            if (hip) HiPunch = 1;           // if any column has Hi Punch Y(12) set, signal it
            if (c2 < 0) c2 = 0;             // nondigits chars interpreted as zero
            d = d * 10 + c2;
            nDigits++;
            if (nDigits == 10) {
                // end of word
                if (NegPunch) {             // has last digit a minus X(11) punch set?
                    d = -d;                 // yes, change sign of word read
                    if (d == 0) NegZero=1;  // word read is minus zero
                }
                break;
            }
        }
        if (nDigits == 0) break;                        // no well-formed word read -> terminate card processing
        if (addr >= 0) WriteDrum(addr++, d, NegZero);   // store word read from card into drum  
        if (eor) break;                                 // end of card sensed -> terminate card processing

    } 
    return HiPunch;
}


t_int64 decode_num_word(char * buf, int nDigits, int bSpaceIsZero) 
{
    t_int64 d;
    int i,c;

    d = 0;
    for (i=0;i<nDigits;i++) {
        c = *buf++;
        if ((c == 32) && (bSpaceIsZero)) c = '0';
        if ((c < '0') || (c > '9')) {
            d = -1; // not a number
            break;
        }
        d = d * 10 + c - '0';
    }
    if (d < 0) {
        // not a number -> return all 9's
        d = 0;
        for (i=0;i<nDigits;i++) d = d * 10 + 9;
    }
    return d;
}

t_int64 decode_alpha_word(char * buf, int n) 
{
    t_int64 d;
    int i;

    d = 0;
    for (i=0;i<n;i++) {
        d = d * 100 + ascii_to_NN(*buf++);
    }
    return d;
}

void decode_soap_wiring(char * buf, int addr) 
{
    // decode soap card simulating soap control panel wiring for 533 
    // from SOAP II manual at http://www.bitsavers.org/pdf/ibm/650/24-4000-0_SOAPII.pdf
    // input card
    //    Column:   41 | 42 | 43 44 45 46 47 | 48 49 50 | 51 52 53 54 55 | 56 | 57 58 59 60 61 | 62 | 63 64 65 66 67 68 69 70 71 72
    //              Ty | Sg |    Location    |  OpCode  |   Data Addr    | Tg |  Instr Addr    | Tg | Remarks
    //
    //    Ty = Type = blank, 1 or 2
    //    Sg = sign = blank or -
    //    Tg = Tag  = 
    // storage in input block
    //    Word 1951:  | <-  Location   -> | Alphabetic
    //         1952:  | <-  Data Addr  -> | Alphabetic
    //         1953:  | <-  Inst Addr  -> | Alphabetic
    //                +-+-+-|-+-+-|-+-|-+-|
    //         1954:  |   Op Code |DTg|ITg| Alphabetic
    //                +-+-+-|-+-+-|-+-|-+-|
    //         1955:  | <- Remarks     -> | Alphabetic
    //         1956:  | <- Remarks     -> | Alphabetic
    //                +-+-+-+-+-+-|-+-+-+-|
    //         1957:  |           |N N N N| L Absolute Part
    //         1958:  |           |N N N N| D Absolute Part
    //         1959:  |           |N N N N| I Absolute Part
    //         1960:  |             |T b n| T=Type (0 if Blank), b=0/8 (for non blank type), n=0/8 (for negative)
    //                              
    int ty,neg;

    DRUM[addr + 0] = decode_alpha_word(&buf[42], 5);            // Location (5 chars)
    DRUM[addr + 1] = decode_alpha_word(&buf[50], 5);            // Data Addr (5 chars)
    DRUM[addr + 2] = decode_alpha_word(&buf[56], 5);            // Inst Addr (5 chars)
    DRUM[addr + 3] = decode_alpha_word(&buf[47], 3) * D4  +     // OpCode (3 chars only)
                     decode_alpha_word(&buf[55], 1) * 100 +     // Data Addr Tag (1 char only)
                     decode_alpha_word(&buf[61], 1);            // Instr Addr Tag (1 char only)
    DRUM[addr + 4] = decode_alpha_word(&buf[62], 5);            // Remarks
    DRUM[addr + 5] = decode_alpha_word(&buf[67], 5);            // Remarks

    DRUM[addr + 6] = decode_num_word(&buf[43], 4, 0);           // Absolute Part of location
    DRUM[addr + 7] = decode_num_word(&buf[51], 4, 0);           // Absolute Part of Data Addr
    DRUM[addr + 8] = decode_num_word(&buf[57], 4, 0);           // Absolute Part of Instr Addr

    if (buf[40] == '1') {ty = 18; } else
    if (buf[40] == '2') {ty = 28; } else {ty = 0; }
    neg = (buf[41] == '-') ? 8:0;

    DRUM[addr + 9] = ty * 10 + neg;                                 // |T b n| T=Type (0 if Blank), b=0/8 (for non blank type), n=0/8 (for negative)
}

int sformat(char * buf, const char * match)
{
    char m,c;

    while(1) {
        m = *match++;
        if (m == 0) break;
        c = *buf++;
        if (c == 0) return 0; // end of buf str before end of match string -> return 0 -> buf does not match
        if ((m == ' ') && (c == ' ')) continue;
        if ((m == 'N') && (c >= '0') && (c <= '9')) continue;
        if ((m == '+') && ((c == '+') || (c == '-'))) continue;
        return 0;             // buf does not match -> return 0 -> buf does not match
    }
    return 1; // end of match string -> return 1 -> buf matches
}

void decode_is_wiring(char * buf, int addr) 
{
    // decode Floationg Decimal Interpretive System (IS) card simulating control panel wiring for 533 as described 
    // in manual at http://www.bitsavers.org/pdf/ibm/650/28-4024_FltDecIntrpSys
    // input card
    //    Column:    1 2 3 4 |  5  6 |  7  8  9 | 10 | 11 | 12 - 21 | 22 | 23 - 32 | 33 | 34 - 43 | 44 | 45 - 54 | 55 | 56 - 65 | 66 | 67 - 76 | 77 78 79 | 80
    //                 Card  |       | Location | wc | s1 |  Word1  | s2 |  Word2  | s3 |  Word3  | s4 |  Word4  | s5 |  Word5  | s6 |  Word6  | Problem  | 
    //                 Num   |                                                                                                                   Num
    //
    //    wc = Word Count (space for 1)
    //    s1 = sign of word 1 (space for +)
    //    Tr = Tracing identification
    //
    // Alternate input format to allow system deck loading 
    //    Column:    1 2 |  3 | 4  5  6 | 7 | 8 9 10 11 | 12 | 13 - 24       
    //              Deck | sp |   Card  |   |   NNNN    |    | NN NNNN NNNN
    //               Num |    |   Num   |                         
    //
    // Alternate input format to allow IT source program loading 
    //    Column:    1 2 3 4 |  5  6 |  7  8  9 | 10 | 11 | 12 - 24
    //                 Card  | Blank | Location |    | sg | N NNN NNN NNN  <- This is an IT instruction (format O1 A B C)
    //                 Num   | 
    //    Column:    1 2 3 4 |  5  6 |  7  8  9 | 10 | 11 | 12 - 23
    //                 Card  | Blank | Location |    | sg | N NNNNNNN NN   <- This is an IT float numeric constant (mantissa and exponent)
    //                 Num   | 
    //    Column:    1 2 3 4 |  5  6 |  7  8  9 | 10 - 23
    //                 Card  | Blank | Location | blanks                   <- This is an IT transfer card (location is start of IT program) 
    //                 Num   | 
    //
    // storage in input block
    //                +-+-+-+-+-+-|-+-+-+-|
    //    Word 1951:  |   |N N N N|       | Location
    //         1952:  |   |N N N N|       | Word Count
    //                +-------------------+ 
    //         1953:  |       word1       | 
    //         1954:  |       word2       | 
    //         1955:  |       word3       | 
    //         1956:  |       word4       | 
    //         1957:  |       word5       | 
    //         1958:  |       word6       | 
    //                +-------------------+ 
    //         1959:  |  Problem Number   | 
    //                +-------------------+ 
    // input card
    //       WordN is 0..9,<space> 
    //       sign  is -,+,<space>
    // put the decoded data in drum at addr (if addr < 0 -> do not store in drum)
    // card number is ignored on reading

    int wc,neg,i;
    int NegZero; 
    t_int64 d;

    if (           sformat(&buf[6], "                   ")) {
       // blank card: read as all zero, one word count
       // this allows to have blank cards/comments card as long as the comment starts on column 27 of more
       DRUM[addr + 1] = 1 * D4;                                              // word count 
    } else if (    sformat(&buf[5], " NNN   ")) {
       // alternate format for loading IT program (IT transfer card)
       DRUM[addr + 0] = decode_num_word(&buf[6], 3, 0) * D4;                 // start location (3 digits)
       DRUM[addr + 1] = 0;                                                   // word count = 0
    } else if (    sformat(&buf[5], " NNN +N NNN NNN NNN ")) {
       // alternate format for loading IT program (IT instruction)
       DRUM[addr + 0] = decode_num_word(&buf[6], 3, 0) * D4;                 // location (3 digits)
       DRUM[addr + 1] = 1 * D4;                                              // word count 
       NegZero = 0;
       neg = (buf[10] == '-') ? 1:0; 
       d   = decode_num_word(&buf[11], 1, 0) * 10 * D8 +          // O1
             decode_num_word(&buf[13], 3, 0) * 100 * D4 +         // O2 or A   
             decode_num_word(&buf[17], 3, 0) * 1000 +             // B         
             decode_num_word(&buf[21], 3, 0);                     // C
       if (neg) {
           d=-d;
           if (d==0) NegZero = 1;
       }
       WriteDrum(addr + 2, d, NegZero);
    } else if (    sformat(&buf[5], " NNN +N NNNNNNN NN ")) {
       // alternate format for loading IT program (numeric constant in float format)
       DRUM[addr + 0] = decode_num_word(&buf[6], 3, 0) * D4;                 // location (3 digits)
       DRUM[addr + 1] = 1 * D4;                                              // word count 
       NegZero = 0;
       neg = (buf[10] == '-') ? 1:0; 
       d   = decode_num_word(&buf[11], 1, 0) * 10 * D8 +          // integer part of mantissa
             decode_num_word(&buf[13], 7, 0) * 100 +              // factional part of mantissa
             decode_num_word(&buf[21], 2, 0);                     // exponent
       if (neg) {
           d=-d;
           if (d==0) NegZero = 1;
       }
       WriteDrum(addr + 2, d, NegZero);
    } else if (   (sformat(&buf[6], " NNNN NN NNNN NNNN ")) || 
                  (sformat(&buf[6], " NNNN NN      NNNN ")) || 
                  (sformat(&buf[6], " NNNN NN NNNN      ")) || 
                  (sformat(&buf[6], " NNNN NN           "))
              ) {
       // alternate format for loading main IT system deck
       DRUM[addr + 0] = decode_num_word(&buf[7], 4, 0) * D4;              // location (4 digits)
       DRUM[addr + 1] = 1 * D4;                                           // word count = 1
       DRUM[addr + 2] = decode_num_word(&buf[12], 2, 1) * D8 +            // op
                        decode_num_word(&buf[15], 4, 1) * D4 +            // data address
                        decode_num_word(&buf[20], 4, 1);                  // instr addr, no negative zero allowed
    } else {
       // regular IT read/punch format
       DRUM[addr + 0] = decode_num_word(&buf[6], 3, 0) * D4;                 // location (3 digits)
       wc = (int) decode_num_word(&buf[9], 1, 1);
       if (wc > 6) wc = 6;
       DRUM[addr + 1] = wc * D4;                                             // word count 
       for (i=0;i<wc;i++) {
          NegZero = 0;
          neg = (buf[10 + 11*i] == '-') ? 1:0;
          d   = decode_num_word(&buf[11 + 11*i], 10, 1);
          if (neg) {
              d=-d;
              if (d==0) NegZero = 1;
          }
          WriteDrum(addr + 2 + i, d, NegZero);
       }
       DRUM[addr + 9] = decode_num_word(&buf[76], 3, 1);                    // problem number
    }
}



/*
 * Device entry points for card reader.
 */
uint32 cdr_cmd(UNIT * uptr, uint16 cmd, uint16 addr)
{
    int i,c;
    struct _card_data   *data;
    char buf[81]; 
    int buf_len;
    uint32              wiring;

    /* Are we currently tranfering? */
    if (uptr->u5 & URCSTA_BUSY)
        return SCPE_BUSY;

    // clear read buffer in drum (where words read from cards will be stored)
    for (i=0;i<10;i++) WriteDrum(addr + i, 0, 0);

    /* Test ready */
    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_CMD, &cdr_dev, "No cards (no file attached)\r\n");
        return SCPE_NOCARDS;
    }

    /* read the cards */
    sim_debug(DEBUG_CMD, &cdr_dev, "READ\r\n");
    uptr->u5 |= URCSTA_BUSY;

    switch(sim_read_card(uptr)) {
    case SCPE_EOF:
         sim_debug(DEBUG_DETAIL, &cdr_dev, "EOF\r\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case SCPE_UNATT:
         sim_debug(DEBUG_DETAIL, &cdr_dev, "Not Attached\r\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case SCPE_IOERR:
         sim_debug(DEBUG_DETAIL, &cdr_dev, "ERR\r\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case SCPE_OK:
         break;
    }

    data = (struct _card_data *)uptr->up7;

    // make local copy of card
    buf_len = data->ptr;
    if (buf_len == 0) {
        buf_len = data->len;
    }
    for (i=0;i<80;i++) {    
        if (i < buf_len) {
            c = data->cbuff[i];
            if (c < ' ') c = ' ';
            buf[i] = c;
        } else {
            buf[i] = ' ';
        }
    }
    buf[80] = 0; // terminate string

    // trim right spaces for printing read card
    for (i=80;i>=0;i--) if (buf[i] > 32) break;
    c = buf[i+1]; buf[i+1]=0;
    sim_debug(DEBUG_DETAIL, &cpu_dev, "Read Card: %s\r\n", buf);
    buf[i+1]=c;

    // check if it is a load card (Y(12) = HiPunch set on any column of card) signales it
    if (decode_8word_wiring(buf, -1)) {
         uptr->u5 |= URCSTA_LOAD;
    } else {
         uptr->u5 &= ~URCSTA_LOAD;
    }

    wiring = (uptr->flags & UNIT_CARD_WIRING);

    // translate chars read from card and copy to drum memory words
    // using the control panel wiring. 
    if (uptr->u5 & URCSTA_LOAD) {
        // load card -> use 8 words per card encoding
        decode_8word_wiring(buf, addr);
    } else if (wiring == WIRING_SOAP) {
        // decode soap card simulating soap control panel wiring for 533 (gasp!)
        decode_soap_wiring(buf, addr);
    } else if (wiring == WIRING_IS) {
        // decode it card 
        decode_is_wiring(buf, addr);
    } else {
        // default wiring: decode up to 8 numerical words per card. Can be a load card
        decode_8word_wiring(buf, addr);
    }
   
    uptr->u5 &= ~URCSTA_BUSY;

    return SCPE_OK;
}

/* Handle transfer of data for card reader */

t_stat
cdr_srv(UNIT *uptr) {

    // I/O is synchronous. No need to set up svr
    return SCPE_OK;
}

/* Set card read/punch control panel wiring */
t_stat cdr_set_wiring (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int f;

    if (uptr == NULL) return SCPE_IERR;
    if (cptr == NULL) return SCPE_ARG;
    for (f = 0; wirings[f].name != 0; f++) {
        if (strcmp (cptr, wirings[f].name) == 0) {
            uptr->flags = (uptr->flags & ~UNIT_CARD_WIRING) | wirings[f].mode;
            return SCPE_OK;
            }
        }
    return SCPE_ARG;
}

/* Show card read/punch control panel wiring */
t_stat cdr_show_wiring (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int f;

    for (f = 0; wirings[f].name != 0; f++) {
        if ((uptr->flags & UNIT_CARD_WIRING) == wirings[f].mode) {
            fprintf (st, "%s wiring", wirings[f].name);
            return SCPE_OK;
        }
    }
    fprintf (st, "invalid control panel wiring (%d)", uptr->flags & UNIT_CARD_WIRING);
    return SCPE_OK;
}


t_stat
cdr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 = 0;
    uptr->u4 = 0;
    uptr->u6 = 0;
    return SCPE_OK;
}

t_stat
cdr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\r\n\r\n", cdr_description(dptr));
   fprintf (st, "The 533 Card Read-punch supported a load mode, and\r\n");
   fprintf (st, "several predefined control panel wiring. Default\r\n");
   fprintf (st, "wiring is up to 8 numeric words per card.\r\n\r\n");
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdr_description(DEVICE *dptr)
{
   return "533 Card Read-Ounch unit";
}


