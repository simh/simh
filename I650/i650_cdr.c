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

#define UNIT_CDR        UNIT_ATTABLE | UNIT_RO | MODE_026 | MODE_LOWER


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

UNIT                cdr_unit[4] = {
   {UDATA(cdr_srv, UNIT_CDR, 0), 300},  // Unit 0 used internally for carddeck operations simulator specific command
   {UDATA(cdr_srv, UNIT_CDR, 0), 300},  // unit 1 is default for initial model (1954)  
   {UDATA(cdr_srv, UNIT_CDR, 0), 300},  // storage unit (1955) allows two extra card/readers for a total of 3
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

// buffer to hold read cards in take hopper of each unit
// to be printed by carddeck command
char ReadHopper[3 * MAX_CARDS_IN_READ_TAKE_HOPPER * 80];
int  ReadHopperLast[3];

// get 10 digits word with sign from card buf (the data struct). return 1 if HiPunch set on any digit
int decode_8word_wiring(struct _card_data * data, int bCheckForHiPunch) 
{
    // decode up to 8 numerical words per card 
    // input card
    //       NNNNNNNNNN ... 8 times
    //       If last digit of word has X(11) punch whole word is set as negative value
    //       If N is non numeric, a 0 is assumed
    // put the decoded data in IO Sync buffer (if bCheckForHiPunch = 1 -> do not store in IO Sync Buffer)
    // return 1 if any colum has Y(12) hi-punch set
    uint16 c1,c2;
    int wn,iCol,iDigit;
    int HiPunch, NegPunch, NegZero; 
    t_int64 d;

    NegZero = 0;                                    // flag set if negative zero is read
    HiPunch = 0;                                    // set to 1 if Y(12) high punch found
    iCol = 0;                                       // current read colum in card
    for (wn=0;wn<8;wn++) {                          // one card generates 8 words in drum mem
        d = NegPunch = 0;
        // read word digits
        for (iDigit=0;iDigit<10;iDigit++) {
            c1 = data->image[iCol++];
            c2 = data->hol_to_ascii[c1];            // convert to ascii
            if ((c1 == 0xA00) || (c2 == '?')) {
                c1 = 0xA00; c2 = '?';               // the punched value +0 should be represented by ascii ? 
            }
            if ((c2 == '+') && (iCol == 1)) {       // on IT control card, first char is a Y(12) punch to make control card a load card. 
                c1 = 0xA00; c2 = '?';               // Digit interpreted as +0
            }
            if (strchr(digits_ascii, c2) == NULL) { // scan digits ascii to check if this is a valid numeric digit with Y or X punch
                c1 = 0;                             // nondigits chars interpreted as blank
            }
            if (c1 & 0x800) HiPunch  = 1;       // if column has Hi Punch Y(12) set, signal it
            NegPunch = (c1 & 0x400) ? 1:0;      // if column has minus X(11) set, signal it
            c1 = c1 & 0x3FF;                    // remove X and Y punches
            c2 = data->hol_to_ascii[c1];        // convert to ascii again
            c2 = c2 - '0';                      // convert ascii to binary digit 
            if (c2 > 9) c2 = 0;                 // nondigits chars interpreted as zero
            d = d * 10 + c2;
        }
        // end of word. set sign
        if (NegPunch) {                         // has last digit a minus X(11) punch set?
            d = -d;                 // yes, change sign of word read
            if (d == 0) NegZero=1;  // word read is minus zero
        }
        
        if (bCheckForHiPunch == 0) {
            IOSync                 [wn]=d;
            IOSync_NegativeZeroFlag[wn]=NegZero;
        }
    } 
    return HiPunch;
}

// load soap symbolic info, This is a facility to help debugging of soap programs into SimH 
// does not exist in real hw
void decode_soap_symb_info(struct _card_data * data) 
{
    t_int64 d;
    int op,da,ia,i,i2,p;
    char buf[81];
    uint16 c1,c2;

    // check soap 1-word load card initial word
    d = IOSync[0];
    if (d != 6919541953LL) return; // not a 1-word load card

    // get the address where the 1-word card will be loaded (into da)
    d  = IOSync[2];
    op = Shift_Digits(&d, 2);               // current inst opcode
    da = Shift_Digits(&d, 4);               // addr of data 
    ia = Shift_Digits(&d, 4);               // addr of next instr
    if ((op != 24) && (ia != 8000)) return; // not a 1-word load card
    if (da >= (int)DRUMSIZE) return;        // symbolic info can only be associated to drum addrs

    // convert card image punches to ascii buf for processing, starting at col 40
    // keep 026 fortran charset
    for (i=40;i<80;i++) {
        c1 = data->image[i];
        c2 = data->hol_to_ascii[c1];        
        c2 = (strchr(mem_to_ascii, toupper(c2))) ? c2:' ';      
        if (c2 == '~') c2 = ' ';
        buf[i] = (char) c2; 
    }
    buf[80] = 0; // terminate string

    // copy soap symbolic info
    i2 = 80;
    while (1) {                             // calc i2 = last non space char to copy
        if (--i2 < 41) return;              // noting to copy
        if (buf[i2] > 32) break;
    }
    p = da * 80;
    for (i=0;i<80;i++) 
        DRUM_Symbolic_Buffer[p+i] = 0;      // clear drum[da] symbolic info
    for (i=41;i<=i2;i++) {
        if ((i==47) || (i==50) || (i==55)) DRUM_Symbolic_Buffer[p++] = 32; // add space separation between op, da, ia fields
        DRUM_Symbolic_Buffer[p++] = buf[i]; 
    }
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


void decode_soap_wiring(struct _card_data * data) 
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
    //                +-------------------+ 
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
    //                +-------------------+ 
    //                              
    int ty,neg;
    char buf[81];
    int i;
    uint16 c1,c2;

    // convert card image punches to ascii buf for processing
    // keep 026 fortran charset
    for (i=0;i<80;i++) {
        c1 = data->image[i];
        c2 = data->hol_to_ascii[c1];        
        c2 = (strchr(mem_to_ascii, toupper(c2))) ? c2:' ';      
        if (c2 == '~') c2 = ' ';
        buf[i] = (char) c2; 
    }
    buf[80] = 0; // terminate string

    IOSync[0] = decode_alpha_word(&buf[42], 5);            // Location (5 chars)
    IOSync[1] = decode_alpha_word(&buf[50], 5);            // Data Addr (5 chars)
    IOSync[2] = decode_alpha_word(&buf[56], 5);            // Inst Addr (5 chars)
    IOSync[3] = decode_alpha_word(&buf[47], 3) * D4  +     // OpCode (3 chars only)
                decode_alpha_word(&buf[55], 1) * 100 +     // Data Addr Tag (1 char only)
                decode_alpha_word(&buf[61], 1);            // Instr Addr Tag (1 char only)
    IOSync[4] = decode_alpha_word(&buf[62], 5);            // Remarks
    IOSync[5] = decode_alpha_word(&buf[67], 5);            // Remarks

    IOSync[6] = decode_num_word(&buf[43], 4, 0);           // Absolute Part of location
    IOSync[7] = decode_num_word(&buf[51], 4, 0);           // Absolute Part of Data Addr
    IOSync[8] = decode_num_word(&buf[57], 4, 0);           // Absolute Part of Instr Addr

    ty = buf[40] - '0';
    if ((ty < 0) || (ty > 9)) ty = 0;
    neg = (buf[41] == '-') ? 8:0;

    IOSync[9] = ty * 100 + 
                (ty ? 80:0) +
                 neg;                                      // |T b n| T=Type (0 if Blank), b=0/8 (for non blank type), n=0/8 (for negative)
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

void decode_is_wiring(struct _card_data * data) 
{
    // decode Floationg Decimal Interpretive System (IS) card simulating control panel wiring for 533 as described 
    // in manual at http://www.bitsavers.org/pdf/ibm/650/28-4024_FltDecIntrpSys.pdf
    // input card
    //    Column:    1 2 3 4 |  5  6 |  7  8  9 | 10 | 11 | 12 - 21 | 22 | 23 - 32 | 33 | 34 - 43 | 44 | 45 - 54 | 55 | 56 - 65 | 66 | 67 - 76 | 77 78 79 | 80
    //                 Card  |       | Location | wc | s1 |  Word1  | s2 |  Word2  | s3 |  Word3  | s4 |  Word4  | s5 |  Word5  | s6 |  Word6  | Problem  | 
    //                 Num   |                                                                                                                   Num
    //
    //    wc   = Word Count (range 0 to 6, space for 1)
    //    s1   = sign of word 1 (-, + or <space> (same as +))
    //    Tr   = Tracing identification
    //    Word = word in format NNNNNNNNNN
    //            N is 0..9, <space> (same as 0)
    //
    // Alternate input format to allow system deck loading 
    //    Column:    1 2 |  3 | 4  5  6 | 7 | 8 9 10 11 | 12 | 13 - 24       
    //              Deck | sp |   Card  |   |   NNNN    |    | NN NNNN NNNN
    //               Num |    |   Num   |                         
    //
    // Alternate input format to allow IT source program loading 
    //    Column:    1 2 3 4 |  5  6 |  7  8  9 | 10 | 11 | 12 - 24
    //                 Card  | Blank | Location |    | sg | N NNN NNN NNN  <- This is an IS instruction (format O1 A B C)
    //                 Num   | 
    //    Column:    1 2 3 4 |  5  6 |  7  8  9 | 10 | 11 | 12 - 23
    //                 Card  | Blank | Location |    | sg | N NNNNNNN NN   <- This is an IS float numeric constant (mantissa and exponent)
    //                 Num   | 
    //    Column:    1 2 3 4 |  5  6 |  7  8  9 | 10 - 23
    //                 Card  | Blank | Location | blanks                   <- This is an IS transfer card (location is start of IT program) 
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
    //
    // card number is ignored on reading

    int wc,neg,i;
    int NegZero; 
    t_int64 d;
    char buf[81];
    uint16 c1,c2;

    // convert card image punches to ascii buf for processing
    // keep 0..9,+,-,<space>, replace anything else by <space>
    for (i=0;i<80;i++) {
        c1 = data->image[i];
        c2 = data->hol_to_ascii[c1];        
        buf[i] = (strchr("+-0123456789", c2)) ? ((char) (c2)):' ';      
    }
    buf[80] = 0; // terminate string

    if (           sformat(&buf[6], "                   ")) {
       // card with firsts 26 cols blank = blank card: read as all zero, one word count
       // this allows to have blank cards/comments card as long as the comment starts on column 27 of more
       IOSync[1] = 1 * D4;                                              // word count 
    } else if (    sformat(&buf[5], " NNN   ")) {
       // alternate format for loading IT program (IT transfer card)
       IOSync[0] = decode_num_word(&buf[6], 3, 0) * D4;                 // start location (3 digits)
       IOSync[1] = 0;                                                   // word count = 0
    } else if (    sformat(&buf[5], " NNN +N NNN NNN NNN ")) {
       // alternate format for loading IT program (IT instruction)
       IOSync[0] = decode_num_word(&buf[6], 3, 0) * D4;                 // location (3 digits)
       IOSync[1] = 1 * D4;                                              // word count 
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
       IOSync                 [2]=d;
       IOSync_NegativeZeroFlag[2]=NegZero;
    } else if (    sformat(&buf[5], " NNN +N NNNNNNN NN ")) {
       // alternate format for loading IT program (numeric constant in float format)
       IOSync[0] = decode_num_word(&buf[6], 3, 0) * D4;                 // location (3 digits)
       IOSync[1] = 1 * D4;                                              // word count 
       NegZero = 0;
       neg = (buf[10] == '-') ? 1:0; 
       d   = decode_num_word(&buf[11], 1, 0) * 10 * D8 +          // integer part of mantissa
             decode_num_word(&buf[13], 7, 0) * 100 +              // factional part of mantissa
             decode_num_word(&buf[21], 2, 0);                     // exponent
       if (neg) {
           d=-d;
           if (d==0) NegZero = 1;
       }
       IOSync                 [2]=d;
       IOSync_NegativeZeroFlag[2]=NegZero;
    } else if (   (sformat(&buf[6], " NNNN NN NNNN NNNN ")) || 
                  (sformat(&buf[6], " NNNN NN      NNNN ")) || 
                  (sformat(&buf[6], " NNNN NN NNNN      ")) || 
                  (sformat(&buf[6], " NNNN NN           "))
              ) {
       // alternate format for loading main IT system deck
       IOSync[0] = decode_num_word(&buf[7], 4, 0) * D4;              // location (4 digits)
       IOSync[1] = 1 * D4;                                           // word count = 1
       IOSync[2] = decode_num_word(&buf[12], 2, 1) * D8 +            // op
                   decode_num_word(&buf[15], 4, 1) * D4 +            // data address
                   decode_num_word(&buf[20], 4, 1);                  // instr addr, no negative zero allowed
    } else {
       // regular IT read/punch format
       IOSync[0] = decode_num_word(&buf[6], 3, 0) * D4;                 // location (3 digits)
       wc = (int) decode_num_word(&buf[9], 1, 1);
       if (wc > 6) wc = 6;
       IOSync[1] = wc * D4;                                             // word count 
       for (i=0;i<wc;i++) {
          NegZero = 0;
          neg = (buf[10 + 11*i] == '-') ? 1:0;
          d   = decode_num_word(&buf[11 + 11*i], 10, 1);
          if (neg) {
              d=-d;
              if (d==0) NegZero = 1;
          }
          IOSync                 [2+i]=d;
          IOSync_NegativeZeroFlag[2+i]=NegZero;
       }
       IOSync[9] = decode_num_word(&buf[76], 3, 1);                    // problem number
    }
}

void decode_it_wiring(struct _card_data * data) 
{
    // decode IT compiler card simulating control panel wiring for 533 
    // from IT manual at http://www.bitsavers.org/pdf/ibm/650/CarnegieInternalTranslator.pdf
    // source program input card
    //    Column:  1  2  3  4 |   5   | 6 - 42 |  43 - 70  | 71 72 |  73 - 80  |
    //               N N N N  |   +   |        | Statement |       | Comments  |
    //              Statement | Y(12) |        |  max 28   |       |  max 8    |
    //                Number  | Punch |        |  chars    |       |  chars    |        
    //
    // storage in input block
    //                +-------------------+ 
    //    Word 0051:  | <-  Statement  -> | Alphabetic
    //         0052:  | <-  Statement  -> | Alphabetic
    //         0053:  | <-  Statement  -> | Alphabetic
    //         0054:  | <-  Statement  -> | Alphabetic
    //         0055:  | <-  Statement  -> | Alphabetic
    //         0056:  | <-  Statement  -> | Alphabetic
    //                +-+-+-+-+-+-|-+-+-+-|
    //         0057:  |           |N N N N| Statement Number
    //                +-+-+-+-+-+-|-+-+-+-|
    //         0058:  |                   | Not used
    //         0059:  |                   | Not used
    //         0060:  |                   | Not used
    //                +-------------------+ 
    //                              
    // type 1 data input card
    //    Column:  1  2 |   3   | 4  5  6 | 7 8 9 10 | 11 - 20 |  
    //              VV  |   +   |  N N N  | D D D D  |  Word
    //                  | Y(12) |
    //                  | Punch |
    //    VV = IT variable being loaded: 01 -> I type, 02 -> Y type, 03 -> C type
    //    N N N = variable number (I5 -> 01 + 005)
    //    D D D D = variable arbitrary non-zero identification number
    //    Word = word to be loaded into IT variable. If type I, is an integer. If type C or Y
    //           type is word is float (M MMMMMMM EE -> M=mantisa, EE=exponent)
    //           if word is negative, last digit get X(11) overpunch
    //    up to 4 pairs var-word per card
    //    last card signaed with a X(11) overpunch in col 10
    //    space is considered as zero
    // type 2 data input card is a load card. No spaces are allowed

    char buf[81];
    int i;
    uint16 c1,c2;

    // convert card image punches to ascii buf for processing
    // keep 026 fortran charset
    for (i=0;i<80;i++) {
        c1 = data->image[i];
        c2 = data->hol_to_ascii[c1];        
        c2 = (strchr(mem_to_ascii, toupper(c2))) ? c2:' ';      
        if (c2 == '~') c2 = ' ';
        buf[i] = (char) c2; 
    }
    buf[80] = 0; // terminate string

    if (buf[2] == '+') {
        // type 1 data card
        // re-read as 8 word per card
        decode_8word_wiring(data, 0);
        return;
    }
    IOSync[0] = decode_alpha_word(&buf[42], 5);            // Statement (5 chars)
    IOSync[1] = decode_alpha_word(&buf[47], 5);            // Statement (5 chars)
    IOSync[2] = decode_alpha_word(&buf[52], 5);            // Statement (5 chars)
    IOSync[3] = decode_alpha_word(&buf[57], 5);            // Statement (5 chars)
    IOSync[4] = decode_alpha_word(&buf[62], 5);            // Statement (5 chars)
    IOSync[5] = decode_alpha_word(&buf[67], 3);            // Statement (3 chars)

    IOSync[6] = decode_num_word(&buf[0], 4, 1);            // Statement Number (space is read as digit zero)

}

void decode_fortransit_wiring(struct _card_data * data) 
{
    // decode FORTRANSIT translator card simulating control panel wiring for 533 
    // from FORTRANSIT manual at http://bitsavers.org/pdf/ibm/650/28-4028_FOR_TRANSIT.pdf
    // implemented Fortransit II (S) 
    // fortran source program input card
    //    Column:  1  |  2 3 4 5  |  6   |  7 - 36   | 37 - 80  |
    //             C  |  N N N N  | cont | Statement |   Blank  |
    //
    //    C = Blank or Comment if C is present
    //    NNNN = Blank or statement number
    //    cont = Blank or non-blank/non-zero for continuation card
    //
    // storage in input block
    //                +-------------------+ 
    //    Word 1951:  | <-  Statement  -> | Alphabetic
    //         1952:  | <-  Statement  -> | Alphabetic
    //         1953:  | <-  Statement  -> | Alphabetic
    //         1954:  | <-  Statement  -> | Alphabetic
    //         1955:  | <-  Statement  -> | Alphabetic
    //         1956:  | <-  Statement  -> | Alphabetic
    //                +-------------------+ 
    //         1957:  |                   | Not used
    //         1958:  |                   | Not used
    //         1959:  |                   | Not used
    //                +-+-+-------+-------+
    //         1960:  |m t|       |N N N N| m = 8/0 (8 -> comment card) 
    //                +---+-------+-------+ t = 8/0 (8 -> continuatin card) 
    //                                      NNNN = statement sumber
    //                   
    // it source program input card
    //    Column:  1  2  3  4 |   5   | 6 - 42 |  43 - 70  | 71 72 |  73 - 80  |
    //               N N N N  |   +   |        | Statement |       | Comments  |
    //              Statement | Y(12) |        |  max 28   |       |  max 8    |
    //                Number  | Punch |        |  chars    |       |  chars    |        
    //
    // storage in input block
    //                +-------------------+ 
    //    Word 0051:  | <-  Statement  -> | Alphabetic
    //         0052:  | <-  Statement  -> | Alphabetic
    //         0053:  | <-  Statement  -> | Alphabetic
    //         0054:  | <-  Statement  -> | Alphabetic
    //         0055:  | <-  Statement  -> | Alphabetic
    //         0056:  | <-  Statement  -> | Alphabetic
    //                +-+-+-+-+-+-|-+-+-+-|
    //         0057:  |           |N N N N| Statement Number
    //                +-+-+-+-+-+-|-+-+-+-|
    //         0058:  |                   | Not used
    //         0059:  |                   | Not used
    //         0060:  |                   | Not used
    //                +-------------------+ 
    //                              
    // fortransit input data card
    //    Column:  1  - 10 | 11 - 20 | 21 - 30 | 31 - 40 | 41 - 50 | 51 - 60 | 61 - 70 | 71 72 |   73  | 74 - 80  |
    //              Word1  |  Word2  |  Word3  |  Word4  |  Word5  |  Word6  |  Word7  |       |   +   |
    //                                                                                         | Y(12) |
    //    Word = word to be loaded into FORTRANSITIT variable. Must match the variable type where it is read in
    //           float (MMMMMMMM EE -> M=mantisa, EE=exponent, 1000000051 is 1.0)
    //           fixed (NNNNNNNNNN -> 000000030J is -302)
    //           if word is negative, last digit get X(11) overpunch
    //       If last digit of word has X(11) punch whole word is set as negative value
    //       If N is non numeric, a 0 is assumed
    //
    // storage in input block
    //                +-------------------+ 
    //    Word 1951:  | <-  Word1      -> | 
    //         1952:  | <-  Word2      -> | 
    //         1953:  | <-  Word3      -> | 
    //         1954:  | <-  Word4      -> | 
    //         1955:  | <-  Word5      -> | 
    //         1956:  | <-  Word6      -> | 
    //         1957:  | <-  Word7      -> | 
    //                +-------------------+
    //         1958:  |                   | Not used
    //         1959:  |                   | Not used
    //         1960:  |                   | Not used
    //                +-------------------+ 
    //                              
    char buf[81];
    int i;
    uint16 c1,c2;

    // convert card image punches to ascii buf for processing
    // keep 026 fortran charset
    for (i=0;i<80;i++) {
        c1 = data->image[i];
        c2 = data->hol_to_ascii[c1]; 
        c2 = toupper(c2);
        c2 = (strchr(mem_to_ascii, c2)) ? c2:' ';      
        if (c2 == '~') c2 = ' ';
        buf[i] = (char) c2; 
    }
    buf[80] = 0; // terminate string

    if (buf[72] == '+') {
        // read data card input for READ fortransit command
        // re-read as 8 word per card
        decode_8word_wiring(data, 0);
        return;
    } else if (buf[4] == '+') {
        // it source statement
        IOSync[0] = decode_alpha_word(&buf[42], 5);            // Statement (5 chars)
        IOSync[1] = decode_alpha_word(&buf[47], 5);            // Statement (5 chars)
        IOSync[2] = decode_alpha_word(&buf[52], 5);            // Statement (5 chars)
        IOSync[3] = decode_alpha_word(&buf[57], 5);            // Statement (5 chars)
        IOSync[4] = decode_alpha_word(&buf[62], 5);            // Statement (5 chars)
        IOSync[5] = decode_alpha_word(&buf[67], 5);            // Statement (5 chars)

        IOSync[6] = decode_num_word(&buf[0], 4, 1);            // Statement Number (space is read as digit zero)
    } else {
        // fortran source statement
        IOSync[0] = decode_alpha_word(&buf[6],  5);            // Statement (5 chars)
        IOSync[1] = decode_alpha_word(&buf[11], 5);            // Statement (5 chars)
        IOSync[2] = decode_alpha_word(&buf[16], 5);            // Statement (5 chars)
        IOSync[3] = decode_alpha_word(&buf[21], 5);            // Statement (5 chars)
        IOSync[4] = decode_alpha_word(&buf[26], 5);            // Statement (5 chars)
        IOSync[5] = decode_alpha_word(&buf[31], 5);            // Statement (5 chars)

        IOSync[9] = (  (buf[0] == 'C')                    ? (t_int64) 80 * D8 : 0  ) +  // is a comment card
                    (  ((buf[5] != ' ') && (buf[5] != 0)) ? (t_int64)  8 * D8 : 0  ) +  // continuation line
                    (  decode_num_word(&buf[1], 4, 1)                              );   // statement number
    }
}
/*
 * Device entry points for card reader.
 */
uint32 cdr_cmd(UNIT * uptr, uint16 cmd, uint16 addr)
{
    struct _card_data   *data;
    uint32              wiring;
    int i;
    char cbuf[81]; 
    int ncdr, ic;

    /* Are we currently tranfering? */
    if (uptr->u5 & URCSTA_BUSY)
        return SCPE_BUSY;

    // clear IO Sync buffer (where words read from cards will be stored)
    for (i=0;i<10;i++) {
       IOSync                 [i]=0;
       IOSync_NegativeZeroFlag[i]=0;
    }

    /* Test ready */
    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_CMD, &cdr_dev, "No cards (no file attached)\n");
        return SCPE_NOCARDS;
    }

    /* read the cards */
    sim_debug(DEBUG_CMD, &cdr_dev, "READ\n");
    uptr->u5 |= URCSTA_BUSY;

    switch(sim_read_card(uptr)) {
    case SCPE_EOF:
         sim_debug(DEBUG_DETAIL, &cdr_dev, "EOF\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case SCPE_UNATT:
         sim_debug(DEBUG_DETAIL, &cdr_dev, "Not Attached\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case SCPE_IOERR:
         sim_debug(DEBUG_DETAIL, &cdr_dev, "ERR\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case SCPE_OK:
         break;
    }

    data = (struct _card_data *)uptr->up7;

    // make local copy of card for debug output
    for (i=0; i<80; i++)
        cbuf[i] = data->hol_to_ascii[data->image[i]];
    cbuf[80] = 0; // terminate string
    sim_debug(DEBUG_DETAIL, &cpu_dev, "Read Card: %s\n", sim_trim_endspc(cbuf));

    // save read card in last read card buffer to be eventually printed
    // by carddec echolast scp command
    ncdr = uptr - &cdr_unit[1];         // ncdr is the card reader: 0 for cdr1, 1 for cdr2, 2 for cdr3
    if ((ncdr >= 0) && (ncdr < 3)) {   // safety check, not needed (should allways be true) but just to be sure
        // advance read buffer last card 
        ReadHopperLast[ncdr] = (ReadHopperLast[ncdr] + 1) % MAX_CARDS_IN_READ_TAKE_HOPPER;
        // save card in read card hopper buffer
        ic = (ncdr * MAX_CARDS_IN_READ_TAKE_HOPPER + ReadHopperLast[ncdr]) * 80;
        for (i=0; i<80; i++) ReadHopper[ic + i] = cbuf[i];
    }

    // uint16 data->image[] array that holds the actual punched rows on card
    // using this codification:
    //
    //  Row Name    value in image[]    comments
    //
    //  Y     0x800               Hi Punch Y(12)
    //  X     0x400               Minus Punch X(11)
    //  0     0x200               also called T (Ten, 10)
    //  1     0x100
    //  2     0x080
    //  3     0x040
    //  4     0x020
    //  5     0x010
    //  6     0x008
    //  7     0x004
    //  8     0x002
    //  9     0x001
    //
    // If several columns are punched, the values are ORed: eg char A is represented as a punch 
    // on row Y and row 1, so it value in image array will be 0x800 | 0x100 -> 0x900

    // check if it is a load card (Y(12) = HiPunch set on any column of card) signales it
    if (decode_8word_wiring(data, 1)) {
         uptr->u5 |= URCSTA_LOAD;
    } else {
         uptr->u5 &= ~URCSTA_LOAD;
    }

    wiring = (uptr->flags & UNIT_CARD_WIRING);

    // translate chars read from card and copy to memory words
    // using the control panel wiring. 
    if (uptr->u5 & URCSTA_LOAD) {
        // load card -> use 8 words per card encoding
        decode_8word_wiring(data, 0);
        if (uptr->u5 & URCSTA_SOAPSYMB) {
            // requested to load soap symb info 
            decode_soap_symb_info(data);
        }
    } else if (wiring == WIRING_SOAP) {
        // decode soap card simulating soap control panel wiring for 533 (gasp!)
        decode_soap_wiring(data);
    } else if (wiring == WIRING_IS) {
        // decode floating point interpretive system (bell interpreter) card 
        decode_is_wiring(data);
    } else if (wiring == WIRING_IT) {
        // decode Carnegie Internal Translator compiler card 
        decode_it_wiring(data);
    } else if (wiring == WIRING_FORTRANSIT) {
        // decode Fortransit translator card 
        decode_fortransit_wiring(data);
    } else {
        // default wiring: decode up to 8 numerical words per card. Can be a load card
        decode_8word_wiring(data, 0);
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
    int ncdr, ic1, ic2, i;

    if (uptr->flags & UNIT_ATT)         // remove current deck in read hopper before attaching
       sim_card_detach(uptr);           // the new one

    r = sim_card_attach(uptr, file);
    if (SCPE_BARE_STATUS(r) != SCPE_OK)
       return r;
    uptr->u5 = 0;
    uptr->u4 = 0;
    uptr->u6 = 0;
    if (sim_switches & SWMASK ('L')) {                    /* Load Symbolic SOAP info?  */
         uptr->u5 |= URCSTA_SOAPSYMB;
    }
    // clear read card take hopper buffer 
    ncdr = uptr - &cdr_unit[1];         // ncdr is the card reader: 0 for cdr1, 1 for cdr2, 2 for cdr3
    if ((ncdr >= 0) && (ncdr < 3)) {   // safety check, not needed (should allways be true) but just to be sure
        // reset last read card number
        ReadHopperLast[ncdr] = 0;
        // clear buffer 
        ic1 = (ncdr * MAX_CARDS_IN_READ_TAKE_HOPPER) * 80;
        ic2 = ic1 + MAX_CARDS_IN_READ_TAKE_HOPPER * 80;
        for (i=ic1; i<ic2; i++) ReadHopper[i] = 0;       
    }

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


