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
uint16 ReadStaker[3 * MAX_CARDS_IN_READ_STAKER_HOPPER * 80];
int    ReadStakerLast[3];

// get 10 digits word with sign from card buf (the data struct). 
// return the first column where HiPunch set (first column is 1; 0 is no HiPunch set)
int decode_8word_wiring(uint16 image[80], int bCheckForHiPunch) 
{
    // decode up to 8 numerical words per card 
    // input card
    //       NNNNNNNNNN ... 8 times
    //       If last digit of word has X(11) punch whole word is set as negative value
    //       If N is non numeric, a 0 is assumed
    // put the decoded data in IO Sync buffer (if bCheckForHiPunch = 1 -> do not store in IO Sync Buffer)
    // return first colum with Y(12) hi-punch set (1 to 80)
    uint16 c1,c2;
    int wn,iCol,iDigit;
    int HiPunch, NegPunch, NegZero; 
    t_int64 d;

    NegZero = 0;                                    // flag set if negative zero is read
    HiPunch = 0;                                    // set if Y(12) high punch found
    iCol = 0;                                       // current read colum in card
    for (wn=0;wn<8;wn++) {                          // one card generates 8 words in drum mem
        d = NegPunch = 0;
        // read word digits
        for (iDigit=0;iDigit<10;iDigit++) {
            c1 = image[iCol++];
            c2 = sim_hol_to_ascii(c1);              // convert to ascii
            if ((c1 == 0xA00) || (c2 == '?')) {
                c1 = 0xA00; c2 = '?';               // the punched value +0 should be represented by ascii ? 
            }
            if ((c2 == '+') && (iCol == 1)) {       // on IT control card, first char is a Y(12) punch to make control card a load card. 
                c1 = 0xA00; c2 = '?';               // Digit interpreted as +0
            }
            if (strchr(digits_ascii, c2) == NULL) { // scan digits ascii to check if this is a valid numeric digit with Y or X punch
                c1 = 0;                             // nondigits chars interpreted as blank
            }
            if (((c1 & 0x800)!=0) && (HiPunch == 0)) {
               HiPunch  = iCol;                     // HiPunch=first column that has Hi Punch Y(12) set
            }
            NegPunch = (c1 & 0x400) ? 1:0;          // if column has minus X(11) set, signal it
            if ((iCol==10) && 
                (c2 == '-')) NegPunch= 1;           // allow a minus on col 10
            c1 = c1 & 0x3FF;                        // remove X and Y punches
            c2 = sim_hol_to_ascii(c1);              // convert to ascii again
            c2 = c2 - '0';                          // convert ascii to binary digit 
            if (c2 > 9) c2 = 0;                     // nondigits chars interpreted as zero
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
void decode_soap_symb_info(uint16 image[80]) 
{
    t_int64 d;
    int op,da,ia,i,i2;
    char buf[81];
    uint16 c1,c2;
    char *Symbolic_Buffer;

    // check soap 1-word load card initial word
    d = IOSync[0];
    if (d != 6919541953LL) return; // not a 1-word load card

    // get the address where the 1-word card will be loaded (into da)
    d  = IOSync[2];
    op = Shift_Digits(&d, 2);               // current inst opcode
    da = Shift_Digits(&d, 4);               // addr of data 
    ia = Shift_Digits(&d, 4);               // addr of next instr
    if ((op != 24) && (ia != 8000)) return; // not a 1-word load card
    if (da < (int)DRUMSIZE) {
        // symbolic info to be associated to drum addrs
        Symbolic_Buffer = &DRUM_Symbolic_Buffer[da * 80];
    } else if ((da >= 9000) && (da < 9060)) {
        // symbolic info to be associated to IAS addrs
        Symbolic_Buffer = &IAS_Symbolic_Buffer[(da - 9000) * 80];
    } else {
        return;                                // symbolic info can only be associated to drum or IAS addrs
    }
    // convert card image punches to ascii buf for processing, starting at col 40
    // keep 026 fortran charset
    i2=0;
    for (i=40;i<80;i++) {
        c1 = image[i];
        c2 = sim_hol_to_ascii(c1);        
        c2 = (strchr(mem_to_ascii, toupper(c2))) ? c2:' ';      
        if (c2 == '~') c2 = ' ';
        if ((i==47) || (i==50) || (i==56)) buf[i2++] = ' '; // add space separation between op, da, ia fields
        buf[i2++] = (char) c2; 
    }
    buf[i2++] = 0; // terminate string

    memset(Symbolic_Buffer, 0, 80);            // clear drum/ias symbolic info
    sim_strlcpy(Symbolic_Buffer, buf, i2);
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


void decode_soap_wiring(uint16 image[80], int bMultiPass) 
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
    //
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
    //                +-------------+-----+ 
    //          
    // If MultiPass flag set, colum 80 contains multipass punches
    //
    // And sets additional flags in 1960 input block
    //
    //                +-+-----+-----+-----+ 
    //         1960:  | |N N N|     |T b n| T=Type (0 if Blank), b=0/8 (for non blank type), n=0/8 (for negative)
    //                +-+-----+-----+-----+ 
    int ty,neg,col80;
    char buf[81];
    int i;
    uint16 c1,c2;

    // convert card image punches to ascii buf for processing
    // keep 026 fortran charset
    for (i=0;i<80;i++) {
        c1 = image[i];
        c2 = sim_hol_to_ascii(c1);        
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
    col80 = buf[79];

    IOSync[9] = ty * 100 + 
                (ty ? 80:0) +
                 neg;                // |T b n| T=Type (0 if Blank), b=0/8 (for non blank type), n=0/8 (for negative)
    if (bMultiPass) {
        IOSync[9] += 9 * ((t_int64) D8      ) +      // Loc addr    digit 9
                     9 * ((t_int64) D8 / 10 ) +      // Data addr   digit 8
                     9 * ((t_int64) D8 / 100) ;      // Instr addr  digit 7
    }
}

void decode_supersoap_wiring(uint16 image[80]) 
{
    // decode supersoap card simulating soap control panel wiring for 533 
    // educated guess based on supersoap program listing at http://archive.computerhistory.org/resources/access/text/2018/07/102784987-05-01-acc.pdf
    // input card
    //    Column: | 23 24 25 26 | 27 .. 32 | 33 34 35 36 | 37 38 39 40 | 41 | 42 | 43 44 45 46 47 | 48 49 50 | 51 52 53 54 55 | 56 | 57 58 59 60 61 | 62 | 63 64 65 66 67 68 69 70 71 72
    //            | LH          |          | DH          | IH          | Ty | Sg |    Location    |  OpCode  |   Data Addr    | Tg |  Instr Addr    | Tg | Remarks
    //
    //    Ty = Type = blank, or 0 to 9
    //    Sg = sign = blank or -
    //    Tg = Tag  A to D
    //    LH, DH, IH can be bank or set (for hand optimization of input card)
    //
    // storage in input block
    //                +-------------------+ 
    //    Word 1951:  | <-  Location   -> | Alphabetic
    //         1952:  | <-  Data Addr  -> | Alphabetic
    //         1953:  | <-  Inst Addr  -> | Alphabetic
    //                +-+-+-+-+-+-+-+-|-+-|
    //         1954:  |   Op Code |DTg|ITg| Alphabetic
    //                +-+-+-|-+-+-|-+-|-+-|
    //         1955:  | <- Remarks     -> | Alphabetic
    //         1956:  | <- Remarks     -> | Alphabetic
    //                +-+-+-+-+-+-|-+-+-+-|
    //         1957:  |   |D D D D|I I I I| DH, IH field for hand optimization 
    //         1958:  |   |N N N N|       | LH field for hand optimization
    //         1959:  |                   | 
    //         1960:  |x x x   n   8     T| T=card type     
    //                +-+-+-+-+-+-+-+-+-+-+
    //
    //         T=card type: 0=assembler source, 1=comment, 
    //                      2/4=non generating code, 3=no_DUP 8 (manual page 40)
    //         n=9 -> positive value, =8 -> negative
    //         x=don't care         
    //
    //                +-------------+-----+ 
    //          
    int ty,neg,col80;
    char buf[81];
    int i;
    uint16 c1,c2;

    // convert card image punches to ascii buf for processing
    // keep 026 fortran charset
    for (i=0;i<80;i++) {
        c1 = image[i];
        c2 = sim_hol_to_ascii(c1);        
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

    IOSync[6] = decode_num_word(&buf[32], 4, 1) * D4 +
                decode_num_word(&buf[36], 4, 1);           // DH & IH
    IOSync[7] = decode_num_word(&buf[22], 4, 1);           // LH
    IOSync[8] = 0;

    ty = buf[40] - '0';
    if ((ty < 0) || (ty > 9)) ty = 0;
    neg = (buf[41] == '-') ? 8:9;
    col80 = buf[79];

    IOSync[9] = ty + 
                neg * 100000 +      // 8=negative, 9=positive XXX
                8 * 1000;

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

void decode_is_wiring(uint16 image[80]) 
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
        c1 = image[i];
        c2 = sim_hol_to_ascii(c1);        
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

void decode_it_wiring(uint16 image[80]) 
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
        c1 = image[i];
        c2 = sim_hol_to_ascii(c1);        
        c2 = (strchr(mem_to_ascii, toupper(c2))) ? c2:' ';      
        if (c2 == '~') c2 = ' ';
        buf[i] = (char) c2; 
    }
    buf[80] = 0; // terminate string

    if (buf[2] == '+') {
        // type 1 data card
        // re-read as 8 word per card
        decode_8word_wiring(image, 0);
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

// convert RrNNNN to word 
// R can be A to I (equivalent to 1 to 9). r and N can be 0 to 9
// any other char assumed to be zero
t_int64 decode_regional_addr(char * buf, char * nbuf)
{
   int c;
   t_int64 w;

   c = *buf++;
   if ((c >= 'A') && (c <= 'I')) {
      w=(c-'A'+1); // convert region letter A-I to digit 1-9
   } else if ((c >= '1') && (c <= '9')) {
      w=(c-'1'+1); 
   } else {w=0;}

   c = *buf++;
   if ((c >= '0') && (c <= '9')) { 
      w = w * 10 + c - '0'; 
   } else {
      w = w * 10;
   } 
   return w * 10000 + decode_num_word(nbuf, 4, 1);
}

int decode_ra_wiring(uint16 image[80], int HiPunch) 
{
    // decode REGIONAL ASSEMBLY card simulating control panel wiring for 533
    // return 1 if it is a load card that makes RD inst continue to DA addr instead of IA addr
    // card format in Appl_Sci_tech_Newsletter_10_Oct55.pdf (bitsavers) page p33 
    //
    // the 533 is used as numeric device. Letters does not means alpha chars, but instead are 
    // used as digit+HiPunch Y(12) (0123456789 -> +ABCDEFGHI) or digit+X(11) (0123456789->-JKLMNOPQR)
    //
    // there are 4 formats allowed. Each format is marked con card by a HiPunch on col 3,5 9 or 11
    //
    // the formats are
    //   HiPunch on column 3 -> five field card: this is standard 650 card from format number [1]
    //                     5 -> machine languaje trace: this is standard 650 card from format number [2]
    //                     7 -> flair trace: this is standard 650 card from format number [3]
    //                    11 -> regional instruction: this is standard 650 card from format number [4]
    //                          note that this format allows a characte "A" to "I" on column 11. The Hi Punch is
    // 
    //   On RA wiring, simulated 533 supports:
    //
    //                                Format   Is Load   Apply
    //   card type                    number    Card?  533 format
    //   -----------------:------- --------- --------- ----------
    //   five field card                [1]      NO       YES      <- RD inst continue to DA addr instead of IA addr
    //   regional instruction           [4]      NO       YES      <- RD inst continue to IA addr
    //   normal card          none               NO        NO      <- RD inst continue to IA addr
    //   normal load card   any other            YES       NO      <- RD inst continue to DA addr instead of IA addr
    //
    // regional assembler source program input card (regional instruction) - standard 650 card from format number [4]
    //
    //    Column: | 1 - 5 | 6 - 10 | 11 12 | 13 - 16 | 17 18 | 19 20 | 21 - 24 | 25 26 | 27 - 30 |
    //            | NNNNN | NNNNN  | r  r  | N N N N | N  N  | r  r  | N N N N | r  r  | N N N N |
    //              Deck  | Seq    | Regional Addr   | Op    | Regional Addr   | Regional Addr   |
    //              Numb.          | for location    | Code  | for Data Addr   | for Instr Addr  
    //            
    //              N is digit 0-9. Blank is interpreted as 0 digit
    //              if rr is blank, value 00
    //                    rr can be numeric or Alfa. If alfa, 1=a, 2=b ... 9=i, so "A2" -> RR=12 and "I9" -> RR=99
    //                    rr can be "A0" .. "I9". Any other char is interpreted as '0'
    //              OpCode, DA or IA can be negative by setting X(11) necative punch
    //
    //
    // storage in input block for card format [4] and [4b] 
    //                +-------------------+ 
    //    Word 0401:  | rr NNNN 0000      | Regional addr for location
    //         0402:  | rr NNNN 0000      | Regional addr for Data Addr
    //         0403:  | rr NNNN 0000      | Regional addr for Instr Addr
    //         0404:  | NN 0000 0000      | if OpCode is numeric (Can be positive or negative) else zero
    //         0405:  |                 N | if OpCode is numeric and negative is -1, else zero
    //         0406:  | <-   OpCode   ->  | if OpCode is Alphabetic, the char codes (5 chars), Else zero
    //                +-------------------+ 
    //         0407:  |                   | Not used
    //         0408:  |                   | Not used
    //         0409:  |                   | Not used
    //         0410:  |                   | Not used
    //                +-------------------+ 
    //                                      
    //                   
    // five field card - standard 650 card from format number [1]
    //
    //    Column: | 1 - 5 | 6 - 10 | 11 - 14 | 15 16 | 17 - 20 | 21 - 24 | 25 - 28 | 29 30 | 31 - 34 | 35 - 38 | 39 - 42 | 43 44 | 45 - 48 | 49 - 52 | 53 - 56 | 57 58 | 59 - 62 | 63 - 66 | 67 - 70 | 71 72 | 73 - 76 | 77 - 80 |
    //            | NNhNN | NNNNN  | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | 
    //              Deck  | Seq    | Addr    | Op    | Data    | Instr   | Addr    | Op    | Data    | Instr   | Addr    | Op    | Data    | Instr   | Addr    | Op    | Data    | Instr   | Addr    | Op    | Data    | Instr   | 
    //              Numb.          | Location| Code  | Addr    | Addr    | Location| Code  | Addr    | Addr    | Location| Code  | Addr    | Addr    | Location| Code  | Addr    | Addr    | Location| Code  | Addr    | Addr    | 
    //                             |   (A1)     (O1)    (D1)     (I1)    |   (A2)     (O2)    (D2)     (I2)    |   (A3)     (O3)    (D3)     (I3)    |   (A4)     (O4)    (D4)     (I4)    |   (A5)     (O5)    (D5)     (I5)    | 
    //                             |               Word 1                |               Word 2                |               Word 3                |               Word 4                |               Word 5                | 
    //
    //              h is digit 0-9 with HiPunch set
    //              if HiPunch is set on last digit of a An, the program will autoexecute at this address
    //
    //
    // storage in input block for card format [1] 
    //                +-------------------+ 
    //    Word 1951:  | 24 (A1) 1903      | Note: if A1 has HiPunch on last digit (Y(12) in col 14), the word generated
    //         1952:  | O1 (D1) (I1)      |          at 1951 will be 24 (A1) (A1)
    //         1953:  | 24 (A2) 1904      |       if A2 has HiPunch on last digit (Y(12) in col 28), the word generated
    //         1954:  | O2 (D2) (I2)      |          at 1953 will be 24 (A2) (A2)
    //         1955:  | 24 (A3) 1905      |       if A3 has HiPunch on last digit (Y(12) in col 42), the word generated
    //         1956:  | O3 (D3) (I3)      |          at 1955 will be 24 (A3) (A3)
    //         1957:  | 24 (A4) 1906      |       if A4 has HiPunch on last digit (Y(12) in col 56), the word generated
    //         1958:  | O4 (D4) (I4)      |          at 1957 will be 24 (A4) (A4)
    //         1959:  | 24 (A5) 1901      |       if A5 has HiPunch on last digit (Y(12) in col 70), the word generated
    //         1960:  | O5 (D5) (I5)      |          at 1959 will be 24 (A5) (A5)
    //                +-------------------+ 
    //                                      

    char buf[81];
    int hbuf[81]; 

    int wsgn[5]; // store sgn of words
    int i, IsLoadCard, IsNeg, NegPunch;
    uint16 c1,c2;
    t_int64 A,I;

    IsLoadCard = NegPunch = 0;
    // init sgn to positive
    for (i=0;i<5;i++) wsgn[i]=1;

    // convert card image punches to ascii buf for processing
    for (i=0;i<80;i++) {
        IsNeg = hbuf[i]=0;
        c1 = image[i];
        c2 = sim_hol_to_ascii(c1); 
        c2 = toupper(c2);
        if ((c1 == 0xA00) || (c2 == '?') || c2 == '+') {
            hbuf[i]=1; c2='0';                  // '0' or blank + HiPunch Y(12)
        } else if ((c2 == '!') || (c2 == '-')) {
            IsNeg = 1; c2= '0';              // '0' or blank + X(11)
        } else if ((c2 >= 'A') && (c2 <= 'I')) {
            hbuf[i]=1; c2=c2-'A'+'1';           // A..I means '1'..'9' + HiPunch Y(12) set
        } else if ((c2 >= 'J') && (c2 <= 'R')) {
            IsNeg = 1; c2=c2-'J'+'1';        // J..R means '1'..'9' + X(11) set
        } else if ((c2 >= '1') && (c2 <= '9')) {
                                                // digit '0'..'9'
        } else {
            c2='0';                             // any other is zero
        }
        if (IsNeg) {   // if column has minus X(11) mark sign of the word n
            if (i<10) { // none
            } else if (i<24) {wsgn[0]=-1; // word 1 negative
            } else if (i<38) {wsgn[1]=-1; // word 2 negative
            } else if (i<52) {wsgn[2]=-1; // word 3 negative
            } else if (i<66) {wsgn[3]=-1; // word 4 negative
            } else {wsgn[4]=-1;}          // word 5 negative
            if ((i>=10) && (NegPunch==0)) NegPunch = i;
        }
        buf[i] = (char) c2;
    }
    buf[80] = 0; // terminate string
    
    if (hbuf[10]) {
       // regional instruction: this is standard 650 card from format number [4]
       //    Column: | 1 - 5 | 6 - 10 | 11 12 | 13 - 16 | 17 18 | 19 20 | 21 - 24 | 25 26 | 27 - 30 |
       //            | NNNNN | NNNNN  | r  r  | N N N N | N  N  | r  r  | N N N N | r  r  | N N N N |
       IsNeg = ((NegPunch >=10) && (NegPunch < 30)) ? -1:1;
       IOSync[0] = decode_regional_addr(&buf[10], &buf[12]) * 10000;         // Regional Location
       IOSync[3] = decode_num_word(&buf[16], 2, 1) * 10000 * 10000 * IsNeg;  // opcode numeric
       IOSync[1] = decode_regional_addr(&buf[18], &buf[20]) * 10000;         // Regional DA
       IOSync[2] = decode_regional_addr(&buf[24], &buf[26]) * 10000;         // Regional IA
       IOSync[4] = IsNeg;                                                    // check if word OP DA IA is negative
       IOSync[5] = 0; 
       if (IOSync[4] < 0) IOSync[3] = -IOSync[3];                           // make opcode negative if word negative
    } else if (hbuf[2]) {
       // five field card - standard 650 card from format number [1]
       //    Column: | 1 - 5 | 6 - 10 | 11 - 14 | 15 16 | 17 - 20 | 21 - 24 | 25 - 28 | 29 30 | 31 - 34 | 35 - 38 | 39 - 42 | 43 44 | 45 - 48 | 49 - 52 | 53 - 56 | 57 58 | 59 - 62 | 63 - 66 | 67 - 70 | 71 72 | 73 - 76 | 77 - 80 |
       //            | NNhNN | NNNNN  | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | 
       //                             |   (A1)     (O1)    (D1)     (I1)    |   (A2)     (O2)    (D2)     (I2)    |   (A3)     (O3)    (D3)     (I3)    |   (A4)     (O4)    (D4)     (I4)    |   (A5)     (O5)    (D5)     (I5)    | 
       //                             |               Word 1                |               Word 2                |               Word 3                |               Word 4                |               Word 5                | 
       //
       A = decode_num_word(&buf[10], 4, 1);
       I = (hbuf[13]) ? A : 1903; // if HiPunch on (A1) last digit, replace 1903 with (A1) value
       IOSync[0] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[1] = decode_num_word(&buf[14], 10, 1) * wsgn[0];

       A = decode_num_word(&buf[24], 4, 1);
       I = (hbuf[27]) ? A : 1904; // if HiPunch on (A2) last digit, replace 1904 with (A1) value
       IOSync[2] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[3] = decode_num_word(&buf[28], 10, 1) * wsgn[1];

       A = decode_num_word(&buf[38], 4, 1);
       I = (hbuf[41]) ? A : 1905; // if HiPunch on (A3) last digit, replace 1905 with (A3) value
       IOSync[4] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[5] = decode_num_word(&buf[42], 10, 1) * wsgn[2];

       A = decode_num_word(&buf[52], 4, 1);
       I = (hbuf[55]) ? A : 1906; // if HiPunch on (A4) last digit, replace 1906 with (A4) value
       IOSync[6] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[7] = decode_num_word(&buf[56], 10, 1) * wsgn[3];

       A = decode_num_word(&buf[66], 4, 1);
       I = (hbuf[69]) ? A : 1901; // if HiPunch on (A5) last digit, replace 1901 with (A5) value
       IOSync[8] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[9] = decode_num_word(&buf[70], 10, 1) * wsgn[4];
    } else {
       decode_8word_wiring(image, 0);
       if (HiPunch > 0) IsLoadCard=1;
    }
    return IsLoadCard;
}

int decode_fds_wiring(uint16 image[80], int HiPunch) 
{
    // decode Interpretive Floating Decimal System card
    // return 1 if it is a load card that makes RD inst continue to DA addr instead of IA addr
    // no card format defined in Appl_Sci_tech_Newsletter_08_Oct54.pdf (bitsavers) page p18
    // guesswork based on bitsavers deck format 5440.2009_INTERPRETIVE_FDS.crd
    // two formats are defined. One that match the 5440.2009_INTERPRETIVE_FDS.crd deck, and a second one 
    // that allows to load a single word, used to enter a FDS program on a friendly way
    //
    // FDS program input card - five word card 
    //
    //    Column: | 1 2 | 3  -  6 | 7  8 | 9  - 12 | 13 - 16 | 17 18 | 19 - 22 | 23 24 | 25 - 28 | 29 - 32 | 33 34 | 35 - 38 | 39 40 | 41 - 44 | 45 - 48 | 49 50 | 51 - 54 | 55 56 | 57 - 60 | 61 - 64 | 65 66 | 67 - 70 | 71 72 | 73 - 76 | 77 - 80 | 
    //            | 8 8 | n n n N | n  n | n n n n | n n n N | 8  8  | n n n N | n  n  | n n n n | n n n N | 8  8  | n n n N | n  n  | n n n n | n n n N | 8  8  | n n n N | n  n  | n n n n | n n n N | 8  8  | n n n N | n  n  | n n n n | n n n N | 
    //                  | Addr    | Op   | Data    | Instr   |       | Addr    | Op    | Data    | Instr   |       | Addr    | Op    | Data    | Instr   |       | Addr    | Op    | Data    | Instr   |       | Addr    | Op    | Data    | Instr   | 
    //                  | Location| Code | Addr    | Addr    |       | Location| Code  | Addr    | Addr    |       | Location| Code  | Addr    | Addr    |       | Location| Code  | Addr    | Addr    |       | Location| Code  | Addr    | Addr    | 
    //                  |   (A1)     (O1)    (D1)     (I1)   |       | (A2)     (O2)    (D2)     (I2)      |       | (A3)     (O3)    (D3)     (I3)      |       | (A4)     (O4)    (D4)     (I4)      |       | (A5)     (O5)    (D5)     (I5)      | 
    //                  |               Word 1               |       |              Word 2                 |       |              Word 3                 |       |              Word 4                 |       |              Word 5                 | 
    //                                                                                                                                                     
    //              n is digit 0-9 
    //              H is digit 0-9 with HiPunch set
    //
    //
    // storage in input block 
    //                +-------------------+ 
    //    Word 1951:  | 24 (A1) 1903      | 
    //         1952:  | O1 (D1) (I1)      | 
    //         1953:  | 24 (A2) 1904      | 
    //         1954:  | O2 (D2) (I2)      | 
    //         1955:  | 24 (A3) 1905      | 
    //         1956:  | O3 (D3) (I3)      | 
    //         1957:  | 24 (A4) 1906      | 
    //         1958:  | O4 (D4) (I4)      | 
    //         1959:  | 24 (A5) 1901      | 
    //         1960:  | O5 (D5) (I5)      | 
    //                +-------------------+ 
    //                                      
    // FDS program input card - one word card 
    //
    //    Column: | 1 2 3 | 4  -  7 | 8 9 | 10 11 | 12 | 13  - 16 | 17 | 18 - 21 | 22 23 | 24 - 80
    //            |   + g | n n n n |     | n  n  |    | n n n n  |    | n n n n | s     | comments
    //                    | Addr    |     | Op    |    | Data     |    | Instr   |       | 
    //                    | Location|     | Code  |    | Addr     |    | Addr    |       | 
    //                    |   (A1)  |     |  (O1) |    |  (D1)    |    |  (I1)   |       | 
    //                    |               Word 1                                 |       
    //                                                                                                                                                     
    //              n is digit 0-9 
    //              + is digit 0 with HiPunch set
    //              s is sign. Can be +,- or blank
    //              g can be "G" (7+HiPunch) or blank, If G this is a transfer card to A1 address
    //
    // storage in input block 
    //                +-------------------+ 
    //    Word 1951:  | 24 (A1) 1903      |  if is a transfer card (G present), then this word is:  00 (A1) (A1)
    //         1952:  | O1 (D1) (I1)      | 
    //         1953:  | 24 0000 1904      | 
    //         1954:  | 00 0000 0000      | 
    //         1955:  | 24 0000 1905      | 
    //         1956:  | 00 0000 0000      | 
    //         1957:  | 24 0000 1906      | 
    //         1958:  | 00 0000 0000      | 
    //         1959:  | 24 0000 1901      | 
    //         1960:  | 00 0000 0000      | 
    //                +-------------------+ 
    //                                      

    char buf[81];

    int i, IsLoadCard, IsNeg, NegPunch, IsGo, IsSgn;
    uint16 c1,c2;
    t_int64 A,I;

    IsLoadCard = NegPunch = IsGo = IsSgn = 0;
    // init sgn to positive

    // convert card image punches to ascii buf for processing
    for (i=0;i<80;i++) {
        IsNeg =0;
        c1 = image[i];
        c2 = sim_hol_to_ascii(c1); 
        c2 = toupper(c2);
        if ((c1 == 0xA00) || (c2 == '?') || c2 == '+') {
            c2='0';                  // '0' or blank + HiPunch Y(12)
            if (i==1) HiPunch=2;
        } else if ((c2 == '!') || (c2 == '-')) {
            IsNeg = 1; c2= '0';              // '0' or blank + X(11)
            if (i==21) IsSgn=1;  // '-' in column 22
        } else if ((c2 >= 'A') && (c2 <= 'I')) {
            if ((c2 == 'G') && (i==2)) IsGo=1;  // g or G in column 3
            c2=c2-'A'+'1';           // A..I means '1'..'9' + HiPunch Y(12) set
        } else if ((c2 >= 'J') && (c2 <= 'R')) {
            IsNeg = 1; c2=c2-'J'+'1';        // J..R means '1'..'9' + X(11) set
        } else if ((c2 >= '1') && (c2 <= '9')) {
                                                // digit '0'..'9'
        } else {
            c2='0';                             // any other is zero
        }
        buf[i] = (char) c2;
    }
    buf[80] = 0; // terminate string
    
    if (HiPunch==6) {
       // five word card 
       //    Column: | 1 2 | 3  -  6 | 7  8 | 9  - 12 | 13 - 16 | 17 18 | 19 - 22 | 23 24 | 25 - 28 | 29 - 32 | 33 34 | 35 - 38 | 39 40 | 41 - 44 | 45 - 48 | 49 50 | 51 - 54 | 55 56 | 57 - 60 | 61 - 64 | 65 66 | 67 - 70 | 71 72 | 73 - 76 | 77 - 80 | 
       //            | 8 8 | n n n N | n  n | n n n n | n n n N | 8  8  | n n n N | n  n  | n n n n | n n n N | 8  8  | n n n N | n  n  | n n n n | n n n N | 8  8  | n n n N | n  n  | n n n n | n n n N | 8  8  | n n n N | n  n  | n n n n | n n n N | 
       //                  |   (A1)     (O1)    (D1)     (I1)   |       | (A2)     (O2)    (D2)     (I2)      |       | (A3)     (O3)    (D3)     (I3)      |       | (A4)     (O4)    (D4)     (I4)      |       | (A5)     (O5)    (D5)     (I5)      | 
       //                  |              Word 1                |       |               Word 2                |       |               Word 3                |       |               Word 4                |       |               Word 5                | 
       //
       A = decode_num_word(&buf[2], 4, 1);
       I = 1903; 
       IOSync[0] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[1] = decode_num_word(&buf[6], 10, 1);

       A = decode_num_word(&buf[18], 4, 1);
       I = 1904; 
       IOSync[2] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[3] = decode_num_word(&buf[22], 10, 1);

       A = decode_num_word(&buf[34], 4, 1);
       I = 1905; 
       IOSync[4] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[5] = decode_num_word(&buf[38], 10, 1);

       A = decode_num_word(&buf[50], 4, 1);
       I = 1906; 
       IOSync[6] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[7] = decode_num_word(&buf[54], 10, 1);

       A = decode_num_word(&buf[66], 4, 1);
       I = 1901; 
       IOSync[8] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[9] = decode_num_word(&buf[70], 10, 1);

    } else if (HiPunch==2) {
       //    Column: | 1 2 3 | 4  -  7 | 8 9 | 10 11 | 12 | 13  - 16 | 17 | 18 - 21 | 22 23 | 24 - 80
       //            |   + g | n n n n |     | n  n  |    | n n n n  |    | n n n n | s     | comments
       //                    |   (A1)  |     |  (O1) |    |  (D1)    |    |  (I1)   |       | 
       A = decode_num_word(&buf[3], 4, 1);
       I = 1903; 
       IOSync[0] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       if (IsGo) IOSync[0] = A;
       IOSync[1] = decode_num_word(&buf[ 9], 2, 1) * 10000 * 10000 + 
                   decode_num_word(&buf[12], 4, 1) * 10000 + 
                   decode_num_word(&buf[17], 4, 1);
       if (IsSgn) IOSync[1] = -IOSync[1];

       A = 0; I = 1904; 
       IOSync[2] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[3] = 0;

       A = 0; I = 1905; 
       IOSync[4] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[5] = 0;

       A = 0; I = 1906; 
       IOSync[6] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[7] = 0;

       A = 0; I = 1901; 
       IOSync[8] = (t_int64) 24 * 10000 * 10000 + A * 10000  + I;
       IOSync[9] = 0;
    } else {
       decode_8word_wiring(image, 0);
       if (HiPunch > 0) IsLoadCard=1;
    }
    return IsLoadCard;
}

void decode_fortransit_wiring(uint16 image[80]) 
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
        c1 = image[i];
        c2 = sim_hol_to_ascii(c1); 
        c2 = toupper(c2);
        c2 = (strchr(mem_to_ascii, c2)) ? c2:' ';      
        if (c2 == '~') c2 = ' ';
        buf[i] = (char) c2; 
    }
    buf[80] = 0; // terminate string

    if (buf[72] == '+') {
        // read data card input for READ fortransit command
        // re-read as 8 word per card
        decode_8word_wiring(image, 0);
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
    uint32              wiring;
    uint16 image[80];
    int i, HiPunch;
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
        sim_debug(DEBUG_EXP, &cdr_dev, "No cards (no file attached)\n");
        return SCPE_NOCARDS;
    }

    /* read the cards */
    sim_debug(DEBUG_CMD, &cdr_dev, "READ\n");
    uptr->u5 |= URCSTA_BUSY;

    switch(sim_read_card(uptr, image)) {
    case CDSE_EOF:
         sim_debug(DEBUG_EXP, &cdr_dev, "EOF\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case CDSE_EMPTY:
         sim_debug(DEBUG_EXP, &cdr_dev, "Input Hopper Empty\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case SCPE_UNATT:
         sim_debug(DEBUG_EXP, &cdr_dev, "Not Attached\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case CDSE_ERROR:
         sim_debug(DEBUG_EXP, &cdr_dev, "IO ERR\n");
         uptr->u5 = 0;
         return SCPE_NOCARDS;
    case CDSE_OK:
         break;
    }

    // make local copy of card for debug output
    for (i=0; i<80; i++)
        cbuf[i] = sim_hol_to_ascii(image[i]);
    cbuf[80] = 0; // terminate string
    sim_debug(DEBUG_DETAIL, &cpu_dev, "Read Card: %s\n", sim_trim_endspc(cbuf));

    // save read card in last read card buffer to be eventually printed
    // by carddec echolast scp command
    ncdr = uptr - &cdr_unit[1];         // ncdr is the card reader: 0 for cdr1, 1 for cdr2, 2 for cdr3
    if ((ncdr >= 0) && (ncdr < 3)) {   // safety check, not needed (should allways be true) but just to be sure
        // advance read buffer last card 
        ReadStakerLast[ncdr] = (ReadStakerLast[ncdr] + 1) % MAX_CARDS_IN_READ_STAKER_HOPPER;
        // save card in read card hopper buffer
        ic = (ncdr * MAX_CARDS_IN_READ_STAKER_HOPPER + ReadStakerLast[ncdr]) * 80;
        for (i=0; i<80; i++) {
            ReadStaker[ic + i] = image[i];
        }
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

    wiring  = (uptr->flags & UNIT_CARD_WIRING);
    HiPunch = decode_8word_wiring(image, 1);

    // check if it is a load card (Y(12) = HiPunch set on any column of card) signales it
    // Regional Assembler /FDS should process format of Load Cards
    if ((HiPunch > 0) && 
        (wiring != WIRING_RA) &&
        (wiring != WIRING_FDS)) {
       uptr->u5 |= URCSTA_LOAD;
    } else {
       uptr->u5 &= ~URCSTA_LOAD;
    }

    // translate chars read from card and copy to memory words
    // using the control panel wiring. 
    if (uptr->u5 & URCSTA_LOAD) {
        decode_8word_wiring(image, 0);
        if (uptr->u5 & URCSTA_SOAPSYMB) {
            // requested to load soap symb info 
            decode_soap_symb_info(image);
        }
    } else if (wiring == WIRING_SOAP) {
        // decode soap card simulating soap control panel wiring for 533 (gasp!)
        decode_soap_wiring(image, 0);
    } else if (wiring == WIRING_SOAPA) {
        // decode soap card for multipass sopa IIA
        decode_soap_wiring(image, 1);
    } else if (wiring == WIRING_SUPERSOAP) {
        // decode super soap card 
        decode_supersoap_wiring(image);
    } else if (wiring == WIRING_IS) {
        // decode floating point interpretive system (bell interpreter) card 
        decode_is_wiring(image);
    } else if (wiring == WIRING_RA) {
        // decode Missile Systems Division Lockheed Aircraft Corporation - regional assembly card
        if (decode_ra_wiring(image, HiPunch)) {
            uptr->u5 |= URCSTA_LOAD;
        }
    } else if (wiring == WIRING_FDS) {
        // decode Floating Decimal Systems 
        if (decode_fds_wiring(image, HiPunch)) {
            uptr->u5 |= URCSTA_LOAD;
        }
    } else if (wiring == WIRING_IT) {
        // decode Carnegie Internal Translator compiler card 
        decode_it_wiring(image);
    } else if (wiring == WIRING_FORTRANSIT) {
        // decode Fortransit translator card 
        decode_fortransit_wiring(image);
    } else {
        // default wiring: decode up to 8 numerical words per card. Can be a load card
        decode_8word_wiring(image, 0);
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
        ReadStakerLast[ncdr] = 0;
        // clear buffer 
        ic1 = (ncdr * MAX_CARDS_IN_READ_STAKER_HOPPER) * 80;
        ic2 = ic1 + MAX_CARDS_IN_READ_STAKER_HOPPER * 80;
        for (i=ic1; i<ic2; i++) ReadStaker[i] = 0;       
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
   return "533 Card Read-Punch unit";
}


