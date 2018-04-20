/* i650_cdp.c: IBM 650 Card punch.

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

   This is the standard card punch.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "i650_defs.h"
#include "sim_card.h"

#define UNIT_CDP        UNIT_ATTABLE | MODE_026

/* std devices. data structures

   cdp_dev      Card Punch device descriptor
   cdp_unit     Card Punch unit descriptor
   cdp_reg      Card Punch register list
   cdp_mod      Card Punch modifiers list
*/

uint32              cdp_cmd(UNIT *, uint16, uint16);
t_stat              cdp_srv(UNIT *);
t_stat              cdp_reset(DEVICE *);
t_stat              cdp_attach(UNIT *, CONST char *);
t_stat              cdp_detach(UNIT *);
t_stat              cdp_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdp_description(DEVICE *dptr);
t_stat              cdp_set_wiring (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat              cdp_show_wiring (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat              cdp_set_echo (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat              cdp_show_echo (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

UNIT                cdp_unit[] = {
    {UDATA(cdp_srv, UNIT_CDP, 0), 600},      // unit 0 is the printing mechanism of 407
    {UDATA(cdp_srv, UNIT_CDP, 0), 600},      
    {UDATA(cdp_srv, UNIT_CDP, 0), 600},    
    {UDATA(cdp_srv, UNIT_CDP, 0), 600},    
};

MTAB                cdp_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT",  "FORMAT",  &sim_card_set_fmt, &sim_card_show_fmt, NULL, "Set card format"},
    {MTAB_XTD | MTAB_VUN, 0, "WIRING",  "WIRING",  &cdp_set_wiring,   &cdp_show_wiring,   NULL, "Set card punch/print control panel Wiring"},
    {MTAB_XTD | MTAB_VUN, 0, "ECHO",    "ECHO",    &cdp_set_echo,     &cdp_show_echo,     NULL, "Set console printout for punched cards"},
    {MTAB_XTD | MTAB_VUN, 1, "PRINT",   "PRINT",   &cdp_set_echo,     &cdp_show_echo,     NULL, "Set printout on CDP0 unit for punched cards"},
    {0}
};

DEVICE              cdp_dev = {
    "CDP", cdp_unit, NULL, cdp_mod,
    4, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cdp_attach, &cdp_detach,
    &cdp_dib, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &cdp_help, NULL, NULL, &cdp_description
};

static struct card_wirings wirings[] = {
    {WIRING_8WORD,  "8WORD"},
    {WIRING_SOAP,   "SOAP"}, 
    {WIRING_IS,     "IS"}, 
    {0, 0},
};

// vars where card is encoded for punching
char card_buf[120];
int card_nbuf;

// vars where card is encoded for printing
char card_lpt[120];
int card_nlpt;

void encode_char(int cPunch, int cLpt)
{
    if ((cPunch) && (card_nbuf < 80)) {
        card_buf[card_nbuf++] = cPunch;
    }
    if ((cLpt) && (card_nlpt < 120)) {
        card_lpt[card_nlpt++] = cLpt;
    }
}

void encode_lpt_spc(int nSpaces)
{
    while (nSpaces-- >0) encode_char(0, 32); 
}

void encode_lpt_str(const char * buf)
{
    while (*buf) encode_char(0, *buf++); 
}

void encode_lpt_num(t_int64 d, int l)
{
    char s[20];
    int i,n;
    
    d=AbsWord(d);
    for (i=9;i>=0;i--) {
        n = (int) (d % 10);
        d = d / 10;
        s[i] = '0' + n;
    }
    s[10] = 0;
    encode_lpt_str(&s[10-l]);
}

#define     wf_NNNNNNNNNNs      0
#define     wf_NN_NNNN_NNNNs    1
#define     wf_sN_NNNNNNN_NN    3
#define     wf_sN_NNN_NNN_NNN   4

void encode_lpt_word(t_int64 d, int NegZero, int wFormat)
{
    int n;
    int neg=0;

    if (d < 0) {d=-d; neg=1;} else if ((d==0) && (NegZero)) neg=1;
    if (wFormat == wf_NN_NNNN_NNNNs) {
        n = Shift_Digits(&d, 2); encode_lpt_num(n, 2); encode_lpt_spc(1);
        n = Shift_Digits(&d, 4); encode_lpt_num(n, 4); encode_lpt_spc(1);
        n = Shift_Digits(&d, 4); encode_lpt_num(n, 4); 
        encode_char(0, neg ? '-':' '); 
    } else if (wFormat == wf_sN_NNNNNNN_NN) {
        encode_char(0, neg ? '-':'+'); 
        n = Shift_Digits(&d, 1); encode_lpt_num(n, 1); encode_lpt_spc(1);
        n = Shift_Digits(&d, 7); encode_lpt_num(n, 7); encode_lpt_spc(1);
        n = Shift_Digits(&d, 2); encode_lpt_num(n, 2); 
    } else if (wFormat == wf_sN_NNN_NNN_NNN) {
        encode_char(0, neg ? '-':'+'); 
        n = Shift_Digits(&d, 1); encode_lpt_num(n, 1); encode_lpt_spc(1);
        n = Shift_Digits(&d, 3); encode_lpt_num(n, 3); encode_lpt_spc(1);
        n = Shift_Digits(&d, 3); encode_lpt_num(n, 3); encode_lpt_spc(1);
        n = Shift_Digits(&d, 3); encode_lpt_num(n, 3); 
    } else { // default: wFormat == wf_NNNNNNNNNNs
        encode_lpt_num(d,10);
        encode_char(0, neg ? '-':' '); 
    }
}

// set pch_word[10] with encoded word d.
// if d negative, sign on last digit (units digit)
// if bSetHiPuch=1, set HiPunch on last digit. 
// if bSetHiPuch=2, set HiPunch on last digit and on second digit. 
void sprintf_word(char * pch_word, t_int64 d, int NegZero, int bSetHiPuch)
{
    int i,n,neg, hi; 
    
    if (d < 0) {
        neg = 1; 
        d = -d;
    } else if ((d == 0) && (NegZero)) {
        neg = 1; // Negative Zero -> also puncho X(11) on last 0 digit
    } else {
        neg = 0;
    }
    for (i=9;i>=0;i--) {
        hi = 0;
        if ((i==1) && (bSetHiPuch == 2)) hi = 1; // Set Hi Punch on second digit
        if ((i==9) && (bSetHiPuch > 0))  hi = 1; // Set Hi Punch on last digit (units digit)
        n = (int) (d % 10);
        d = d / 10;
        n = n + hi * 10; 
        if ((neg == 1) && (i==9)) n = n + 20;    // Set negative punch X(11) on last digit
        pch_word[i] = digits_ascii[n]; 
    }
    pch_word[10] = 0;
}

void encode_pch_str(const char * buf)
{
    while (*buf) {
        encode_char(*buf++, 0); 
    }
}


void encode_8word_wiring(int addr) 
{
    // encode 8 numerical words per card 
    // get the decoded data from drum at addr 
    int i, NegZero;
    t_int64 d;
    char pch_word[20];

    // punch card
    for(i=0;i<8;i++) {
        ReadDrum(addr + i, &d, &NegZero);
        sprintf_word(pch_word, d, NegZero, 0);
        encode_pch_str(pch_word);   
    }

    // print out card contents
    // 8 words in format NN NNNN NNNN+
    for(i=0;i<8;i++) {
        ReadDrum(addr + i, &d, &NegZero);
        encode_lpt_word(d, NegZero, wf_NN_NNNN_NNNNs);
        encode_lpt_spc(1);
    }
}

void encode_soap_wiring(int addr) 
{
    // encode soap card simulating soap control panel wiring for 533 
    // from SOAP II manual at http://www.bitsavers.org/pdf/ibm/650/24-4000-0_SOAPII.pdf
    // storage in output block
    //    Word 1977:  | <-  Location   -> | Alphabetic
    //         1978:  | <-  Data Addr  -> | Alphabetic
    //         1979:  | <-  Inst Addr  -> | Alphabetic
    //                +-+-+-|-+-+-|-+-|-+-|
    //         1980:  |   Op Code |DTg|ITg| Alphabetic
    //                +-+-+-|-+-+-|-+-|-+-|
    //         1981:  | <- Remarks     -> | Alphabetic
    //         1982:  | <- Remarks     -> | Alphabetic
    //         1983:  |<-Assembled Instr->|
    //                +-+-|-+-+-+-|-+-+-|-|
    //         1984:  |   |N N N N|     |T| N N N N=Location, T=Type (0 if Blank)
    //         1985:  |           |N N N N| N N N N=Card Number
    //         1986:  |a|b|c|d|e|f|g|h|i|j| a = 0/8 (for non blank type)
    //                                      b = 0/8 (negative)
    //                                      c = 0/8 (bypass)
    //                                      d = 0/8 (punch a)               =8 -> do not print Loc op da ir
    //                                      e = 0/8 (punch b)               =8 -> punch availability table
    //                                      f = 0/8 (800X instruction)
    //                                      g = 0/8 (blank out L)
    //                                      h = 0/8 (blank out D)
    //                                      i = 0/8 (blank out I)
    //                                      j = 0/8 (blank out OP)
    //                              
    // SOAP printout format
    //    | Sg |    Location    |  OpCode  |   Data Addr    | Tg |  Instr Addr    | Tg | Remarks  | Drum Addr | NN NNNN NNNN[-] (signed word value at this drum addr)
    // SOAP punch format (load card, 1 word per card)
    // simulates punching over prepunched 1-word load card
    //    |    word1   |       nnnn |  24 addr 800? | NNNNNNNNNN[-] | source soap line
    //    nnnn=card number
    //    addr=drum address where the word is loaded
    //    NNNNNNNNNN=word to be loaded at addr, with sign

    char loc[6], data_addr[6], inst_addr[6], OpCode[6], Data_Tag[6], Instr_Tag[6], rem1[6], rem2[6];
    char pch_word[20];
    t_int64 d, instr;
    int location, CardNum, ty;
    int b_non_blank, neg, b_blk_op, b_blk_i, b_blk_d, b_blk_l, b_800X, b_pch_b, b_pch_a, b_bypass; // punch control flags
    int i, sv_card_nbuf, n;
    int pat1, pat2;

    word_to_ascii(loc,       1, 5, DRUM[addr + 0]);
    word_to_ascii(data_addr, 1, 5, DRUM[addr + 1]);
    word_to_ascii(inst_addr, 1, 5, DRUM[addr + 2]);
    word_to_ascii(OpCode,    1, 3, DRUM[addr + 3]);
    word_to_ascii(Data_Tag,  4, 1, DRUM[addr + 3]);
    word_to_ascii(Instr_Tag, 5, 1, DRUM[addr + 3]);
    word_to_ascii(rem1,      1, 5, DRUM[addr + 4]);
    word_to_ascii(rem2,      1, 5, DRUM[addr + 5]);
    instr    = DRUM[addr + 6];
    location = (int) ((DRUM[addr + 7] / D4) % D4);
    ty       = (int) ( DRUM[addr + 7]       % 10);
    CardNum  = (int) ( DRUM[addr + 8]       % D4);
    d        =  DRUM[addr + 9];
    b_blk_op    = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_blk_i     = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_blk_d     = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_blk_l     = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_800X      = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_pch_b     = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_pch_a     = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_bypass    = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    neg         = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_non_blank = ((int) (d % 10) == 8) ? 1:0; d = d / 10;

    // printf("bits %06d%04d%c ", printfw(DRUM[addr + 9]));    // to echo the status digits of punched card

    // generate card
    if (b_pch_b) {
        // punch availability table (pat pseudo-op output)
        for(i=0;i<8;i++) {
            sprintf_word(pch_word, DRUM[addr + i], 0, 1);
            encode_pch_str(pch_word);     
        }
    } else {
        if (b_pch_a) {
            // punch non generating code card
            encode_pch_str("0?0000800?");             // load card
            sprintf(pch_word, "      %04d", CardNum); // card number
            encode_pch_str(pch_word);    
            encode_pch_str("          ");             // two blank words
            encode_pch_str("          ");
            if (b_non_blank) encode_pch_str("1"); else encode_pch_str(" ");
        } else {
            // punch generating code card  
            if (b_800X) {
                encode_pch_str("6I1954800?");         // load card for word to be stored in 800X addr
            } else {
                encode_pch_str("6I1954195C");         // load card for word to be stored in drum
            }
            sprintf(pch_word, "      %04d", CardNum); // card number
            encode_pch_str(pch_word);    
            sprintf(pch_word, "24%04d800?", location);// addr to place the loaded word
            encode_pch_str(pch_word);    
            sprintf_word(pch_word, AbsWord(instr) * (neg ? -1:1), ((neg) && (instr == 0)) ? 1:0, 1);
            encode_pch_str(pch_word);    
            encode_char(ty == 0 ? ' ' : '0'+ty, 0); 
        }
        encode_pch_str(" ");
        sv_card_nbuf = card_nbuf;  // save pch bufer current pos
        encode_pch_str(loc);       encode_pch_str(OpCode); 
        encode_pch_str(data_addr); encode_pch_str(Data_Tag); 
        encode_pch_str(inst_addr); encode_pch_str(Instr_Tag); 
        encode_pch_str(rem1);      encode_pch_str(rem2); 
        // convert to lowercase for punching
        for (i=sv_card_nbuf;i<card_nbuf;i++) 
            if ((card_buf[i] >= 'A') && (card_buf[i] <= 'Z')) 
                card_buf[i] = card_buf[i] - 'A' + 'a';
        card_buf[card_nbuf] = 0;
    }

    // generate printout
    if (b_pch_b) {
        // print availability table (pat pseudo-op output)
        for(i=0; i<4; i++) {
            d = DRUM[addr + i*2];
            pat1 = (int) ((d / D4) % D4);
            pat2 = (int) ( d       % D4);
            d = DRUM[addr + i*2 + 1];
            encode_lpt_num(pat1, 4);
            encode_lpt_spc(2);
            encode_lpt_num(d, 10);
            encode_lpt_spc(2);
            encode_lpt_num(pat2, 4);
            encode_lpt_spc(5);
        }
    } else if (ty == 1) {
        // print coment line
        encode_lpt_str("1");
        encode_lpt_spc(14);
        encode_lpt_str(loc); encode_lpt_str(OpCode); 
        encode_lpt_str(data_addr); encode_lpt_str(Data_Tag); 
        encode_lpt_str(inst_addr); encode_lpt_str(Instr_Tag); 
        encode_lpt_str(rem1); encode_lpt_str(rem2); 
    } else {
        encode_lpt_spc(1);
        encode_lpt_str(loc); 
        encode_lpt_spc(2); encode_char(0, neg ? '-':' '); encode_lpt_spc(1);
        encode_lpt_str(OpCode); encode_lpt_spc(3); 
        encode_lpt_str(data_addr); encode_lpt_str(Data_Tag); encode_lpt_spc(2); 
        encode_lpt_str(inst_addr); encode_lpt_str(Instr_Tag); encode_lpt_spc(5); 
        encode_lpt_str(rem1); encode_lpt_str(rem2); 
        if (b_pch_a) {
            // blank op -> do not print location and intruction
            if (b_bypass) {
                encode_lpt_spc(4); 
                encode_lpt_str("BYPASS"); 
            }
        } else {
            encode_lpt_spc(4); 
            if (b_blk_l) { encode_lpt_spc(4); } else encode_lpt_num(location, 4); 
            encode_lpt_spc(2); encode_char(0, neg ? '-':' '); encode_lpt_spc(1);
            d = instr;
            n = Shift_Digits(&d, 2); // operation code (2 digits)
            if (b_blk_op) { encode_lpt_spc(2); } else encode_lpt_num(n, 2); 
            encode_lpt_spc(2);
            n = Shift_Digits(&d, 4); // data addr (4 digits)
            if (b_blk_d) { encode_lpt_spc(4); } else encode_lpt_num(n, 4); 
            encode_lpt_spc(2); 
            n = Shift_Digits(&d, 4); // instr addr (4 digits)
            if (b_blk_i) { encode_lpt_spc(4); } else encode_lpt_num(n, 4); 
            encode_lpt_spc(1); 
            if (b_blk_l)  encode_lpt_str("BLANK L"); else
            if (b_blk_op) encode_lpt_str("BLANK OP"); else
            if (b_blk_d)  encode_lpt_str("BLANK D"); else
            if (b_blk_i)  encode_lpt_str("BLANK I");
        }
    }
}

void encode_is_wiring(int addr) 
{
    // encode Floationg Decimal Interpretive System (IS) card simulating control panel wiring for 533 as described 
    // in manual at http://www.bitsavers.org/pdf/ibm/650/28-4024_FltDecIntrpSys
    // storage in output block
    //                +-+-+-+-+-+-|-+-+-+-|
    //    Word 1977:  |Trc|N N N N|       | Location
    //         1978:  |   |N N N N|       | Word Count
    //                +-------------------+ 
    //         1979:  |       word1       | 
    //         1980:  |       word2       | 
    //         1981:  |       word3       | 
    //         1982:  |       word4       | 
    //         1983:  |       word5       | 
    //         1984:  |       word6       | 
    //                +-------------------+ 
    //         1985:  |  Problem Number   | 
    //         1986:  |   |N N N N|       | Card Number
    //                +-------------------+ 
    //                              
    // if word at 1977 is negative, a load card is punched, but no printout is generated
    // if word at 1977 is positive, regular output card format is used on punch
    //    Column:    1 2 3 4 | 5   6 |  7  8  9 | 10 | 11 | 12 - 21 | 22 | 23 - 32 | 33 | 34 - 43 | 44 | 45 - 54 | 55 | 56 - 65 | 66 | 67 - 76 | 77 78 79 | 80
    //                 Card  |   |   | Location | wc | s1 |  Word1  | s2 |  Word2  | s3 |  Word3  | s4 |  Word4  | s5 |  Word5  | s6 |  Word6  | Problem  | 
    //                 Num   |     if location is > 9999, will use column 6                                                                      Num
    //    wordN is printed as +N NNNNNNN NN (IT sci notation)
    //                 
    // IT  printout format for non tracing cards: 
    //    | Location | Word1 | Word2 | Word3 | Word4 | Word5 | Word6 
    //    wordN is printed as +N NNNNNNN NN (IT sci notation)
    //
    // IT  printout format for tracing cards (Trc digits in word 1977 are non-zero): 
    //    | Location | Word1 | Word2 | Word3 | Word4 | Word5 | Word6 
    //    word1 to 3 are printed as +N NNN NNN NNN (IT instruction format)
    //    word4 to 6 are printed as +N NNNNNNN NN (IT sci notation)
    //
    int i, NegZero;
    t_int64 d;
    int CardNum, loc, wc, PrNum, bTraceCard;
    char pch_word[20];
    int bSetHiPunch;

    bSetHiPunch = (DRUM[addr] < 0) ? 2 : 0; // first bSetHiPunch is 2 if word negative (signals a load card must be punched)

    loc        = (int) ((DRUM[addr]   / D4) % D4);
    CardNum    = (int) ((DRUM[addr+9] / D4) % D4);
    wc         = (int) ((DRUM[addr+1] / D4) % D4);
    PrNum      = (int) ( DRUM[addr+8]);
    bTraceCard = (DRUM[addr] / D8) > 0 ? 1 : 0;   // if to higher digits are nonzero -> is a trace card

    if (bSetHiPunch) {
        // punch a load card
        for(i=0;i<8;i++) {
            ReadDrum(addr + i, &d, &NegZero);
            if ((i==0) && (d < 0)) d = -d;         // get absolute value for DRUM[addr + 0]
            sprintf_word(pch_word, d, NegZero, bSetHiPunch);
            if (bSetHiPunch==2) bSetHiPunch = 1;   // if bSetHiPunch is 2 change it to bSetHiPunch = 1
            encode_pch_str(pch_word);     
        }
    } else {
        // punch a card using output format
        if (loc < 1000) {
            sprintf(pch_word, "%04d  %03d%01d", CardNum, loc, wc);
        } else {
            sprintf(pch_word, "%04d %04d%01d", CardNum, loc, wc);
        }
        encode_pch_str(pch_word);     
        for(i=0;i<6;i++) {
            if (i<wc) {
               ReadDrum(addr + i + 2, &d, &NegZero);
               if ((d < 0) || ((d==0) && (NegZero))) {
                   encode_pch_str("-");
                   d = -d;
               } else {
                   encode_pch_str("+");
               }
               sprintf_word(pch_word, d, 0, 0);
               encode_pch_str(pch_word);     
            } else {
               encode_pch_str("           "); // 11 spaces
            }
        }
        if (PrNum < 0) PrNum = 0;
        if (PrNum > 999) PrNum = 999;
        sprintf(pch_word, "%03d", PrNum);
        encode_pch_str(pch_word);     
    }

    if (bSetHiPunch) {
        // load card, does not generate printout
        // mark lpt output buffer to not print
        if (card_nlpt == 0) {
            card_lpt[card_nlpt++] = 0;
        }
    } else {
        // not load card -> do normal printout for card
        if (wc > 6) wc = 6;
        if (loc < 1000) {
            encode_lpt_spc(1);
            encode_lpt_num(loc, 3);
        } else {
            encode_lpt_num(loc, 4);
        }
        for(i=2;i<2+wc;i++) {
            encode_lpt_spc(2);
            ReadDrum(addr + i, &d, &NegZero);
            if ((bTraceCard) && (i<5)) { 
                // if printing a trace card, first three words are printed as intructions (+N NNN NNN NNN)
               encode_lpt_word(d, NegZero, wf_sN_NNN_NNN_NNN);
            } else {
               // print numbers adding spaces to ease reading IT floating point format (+N NNNNNNN NN)
               encode_lpt_word(d, NegZero, wf_sN_NNNNNNN_NN);
            }
        }
    }
}

/* Card punch routine */
uint32 cdp_cmd(UNIT * uptr, uint16 cmd, uint16 addr)
{
    int i,c,h;
    struct _card_data   *data;
    uint32              wiring;

    /* Are we currently tranfering? */
    if (uptr->u5 & URCSTA_BUSY)
        return SCPE_BUSY;

    /* Test ready */
    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_CMD, &cdp_dev, "No cards (no file attached)\r\n");
        return SCPE_NOCARDS;
    }

    // copy and translate drum memory words to chars to punch 
    // using the control panel wiring. 

    wiring = (uptr->flags & UNIT_CARD_WIRING);
    card_nbuf = card_nlpt = 0;

    if (wiring == WIRING_SOAP) {
        // encode soap card simulating soap control panel wiring for 533 (gasp!)
        encode_soap_wiring(addr);
    } else if (wiring == WIRING_IS) {
        // encode it card 
        encode_is_wiring(addr);
    } else if (wiring == WIRING_8WORD) {
        // encode 8 words per card
        encode_8word_wiring(addr);
    } else {
        // default wiring: decode up to 8 numerical words per card
        encode_8word_wiring(addr);
    }

    if ((card_nlpt == 1) && (card_lpt[0] == 0)) {
        // skip this line printout & echo
    } else {
        /* echo? */
        encode_char(0, 13); encode_char(0, 10);
        if (uptr->flags & UNIT_CARD_ECHO) {
            for (i=0;i<card_nlpt;i++) sim_putchar(card_lpt[i]);
        }
        /* printout punched cards? */
        if (uptr->flags & UNIT_CARD_PRINT) {
            // printout will be directed to file attached to CDP0 unit, if any
            if (cdp_unit[0].flags & UNIT_ATT) {
                sim_fwrite(&card_lpt, 1, card_nlpt, cdp_unit[0].fileref);
            }
        }
    }

    // trim right spaces for printing punch card
    card_buf[card_nbuf] = 0;
    sim_debug(DEBUG_DETAIL, &cpu_dev, "Punch Card: %s\r\n", card_buf);

    /* punch the cards */
    data = (struct _card_data *)uptr->up7;
    for (i=0; i<80; i++) {
        if (i >= card_nbuf) {
            c = 32;
        } else {
            c = card_buf[i];
        }
        if (c == 32) {
            // no punch
            data->image[i] = 0;
        } else {
            // punch char
            h = ascii_to_hol[c & 127];
            data->image[i] = h;
        }
    }
    sim_punch_card(uptr, NULL);
    sim_debug(DEBUG_CMD, &cdp_dev, "PUNCH\r\n");
    uptr->u5 |= URCSTA_BUSY;
    uptr->u4 = 0;
            
    uptr->u5 &= ~URCSTA_BUSY;

    return SCPE_OK;

}

/* Handle transfer of data for card punch */
t_stat
cdp_srv(UNIT *uptr) {

    // I/O is synchronous. No need to set up srv
    return SCPE_OK;
}


/* Set card read/punch control panel wiring */
t_stat cdp_set_wiring (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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
t_stat cdp_show_wiring (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
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

/* Set card read/punch echo to console */
t_stat cdp_set_echo (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 u = (uptr - cdp_unit);
    t_stat              r;
    int                 num;

    if (uptr == NULL) return SCPE_IERR;
    if (cptr == NULL) {
        num = 1;  // no param means set (=1)
    } else {
        num = (int) get_uint (cptr, 10, 1, &r);
        if (r != SCPE_OK) return r;
    }
    if (u == 0) {
       sim_printf("this option cannot be set for CDP0\r\n");
       return SCPE_ARG;
    }
    switch(val) {
        case 0:
            if (num== 0) {
                uptr->flags = uptr->flags & ~UNIT_CARD_ECHO;
            } else {
                uptr->flags = uptr->flags | UNIT_CARD_ECHO;
            }
            break;
        case 1:
            if (num== 0) {
                uptr->flags = uptr->flags & ~UNIT_CARD_PRINT;
            } else {
                uptr->flags = uptr->flags | UNIT_CARD_PRINT;
            }
            break;
    }
    return SCPE_OK;
}

/* Show card read/punch control panel wiring */
t_stat cdp_show_echo (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    switch(val) {
        case 0:
            fprintf (st, (uptr->flags & UNIT_CARD_ECHO) ? "ECHO": "No ECHO");
            break;
        case 1:
            fprintf (st, (uptr->flags & UNIT_CARD_PRINT) ? "PRINT": "No PRINT");
            break;
    }
    return SCPE_OK;
}

t_stat
cdp_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 = 0;

    return SCPE_OK;
}

t_stat
cdp_detach(UNIT * uptr)
{
    return sim_card_detach(uptr);
}

t_stat
cdp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\r\n\r\n", cdp_description(dptr));
   fprintf (st, "The 533 Card Read-punch writes cards using the selected\r\n");
   fprintf (st, "control panel wiring to set the format of punched cards.\r\n");
   fprintf (st, "It is possible to simulate a 407 accounting machine for\r\n");
   fprintf (st, "printing using SET CDP1 PRINT=1. In this case, punched\r\n");
   fprintf (st, "cards will be printed to file attached to unit 0 (CDP0).\r\n");
   fprintf (st, "SET CDP ECHO=1 will display on console cards printout.\r\n");

   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdp_description(DEVICE *dptr)
{
   return "533 Card Punch + 407 Accounting for printing";
}


