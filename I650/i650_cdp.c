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

#define UNIT_CDP        UNIT_ATTABLE | MODE_026 | MODE_LOWER

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

UNIT                cdp_unit[4] = {
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
    char pad;
    
    if (l < 0) {
        l=-l; pad = ' '; // if l < 0 pad with space
    } else {
        pad = '0';       // if l > 0 pag with zero
    }
    d=AbsWord(d);
    for (i=9;i>=0;i--) {
        n = (int) (d % 10);
        d = d / 10;
        s[i] = '0' + n;
    }
    s[10] = 0;
    if (pad == ' ') {
        for(i=0;i<9;i++) {
            if (s[i] != '0') break;
            s[i] = ' ';
        }
    }
    encode_lpt_str(&s[10-l]);
}

#define     wf_NNNNNNNNNNs      0
#define     wf_NN_NNNN_NNNNs    1
#define     wf_sN_NNNNNNN_NN    3
#define     wf_sN_NNN_NNN_NNN   4
#define     wf_nnnnnnnnnNs      5
#define     wf_nnnnnnnnnH       6       
#define     wf_NNNNNNNNNN       7
#define     wf_sNNNNNNNNNN      8

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
    } else if (wFormat == wf_nnnnnnnnnNs) {
        encode_lpt_num(d,-10);  // replace leading zeroes by spaces
        encode_char(0, neg ? '-':' '); 
    } else if (wFormat == wf_nnnnnnnnnH) {
        if (d < 10) {
            encode_lpt_spc(9);
        } else {
            encode_lpt_num(d / 10, -9);         // print 9 digits, replacing leading zeroes by spaces
        }
        n = d % 10;
        encode_char(0, (n==0) ? '+':'A'+n-1);   // hi punch on last digit
    } else if (wFormat == wf_NNNNNNNNNN) {
        encode_lpt_num(d,10);
    } else if (wFormat == wf_sNNNNNNNNNN) {
        encode_char(0, neg ? '-':'+'); 
        encode_lpt_num(d,10);
    } else { // default: wFormat == wf_NNNNNNNNNNs
        encode_lpt_num(d,10);
        encode_char(0, neg ? '-':' '); 
    }
}

// set pch_word[10] with encoded word d.
// if d negative, sign on last digit (units digit)
// if bSetHiPuch=1, set HiPunch on last digit. 
// if bSetHiPuch=2, set HiPunch on last digit and on second digit. 
// if bSetHiPuch=3, set HiPunch on third digit 
// if last digit is negative, never set HiPunch even if asked for (a card column cannot have both X(11) and Y(12) punched)
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
        if ((i==1) && (bSetHiPuch == 2)) hi = 1;                                            // Set Hi Punch on second digit
        if ((i==2) && (bSetHiPuch == 3)) hi = 1;                                            // Set Hi Punch on third digit
        if ((i==9) && ( (bSetHiPuch == 1) || (bSetHiPuch == 2)  ) && (neg == 0))  hi = 1;   // Set Hi Punch on last digit (units digit)
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


void encode_8word_wiring(void) 
{
    // encode 8 numerical words per card 
    // get the decoded data from IOSync 
    int i, NegZero;
    t_int64 d;
    char pch_word[20];

    // punch card
    for(i=0;i<8;i++) {
        d = IOSync[i];
        NegZero = IOSync_NegativeZeroFlag[i];
        sprintf_word(pch_word, d, NegZero, 0);
        encode_pch_str(pch_word);   
    }

    // print out card contents
    // 8 words in format NN NNNN NNNN+
    for(i=0;i<8;i++) {
        d = IOSync[i];
        NegZero = IOSync_NegativeZeroFlag[i];
        encode_lpt_word(d, NegZero, wf_NN_NNNN_NNNNs);
        encode_lpt_spc(1);
    }
}

void encode_soap_wiring(int bMultiPass) 
{
    // encode soap card simulating soap control panel wiring for 533 
    // from SOAP II manual at http://www.bitsavers.org/pdf/ibm/650/24-4000-0_SOAPII.pdf
    // storage in output block
    //                +-------------------+ 
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
    //         1986:  |a|b|c|d|e|f|g|h|i|j| punch control word
    //                                      a = 0/8 (for non blank type)    =0 -> bank LOC,OP etc
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
    //    |    word1   |       nnnn |  24 addr 800? | NNNNNNNNNN | source soap line
    //    nnnn=card number
    //    addr=drum address where the word is loaded
    //    NNNNNNNNNN=word to be loaded at addr, with sign in last digit
    //
    // If MultiPass flag set, 
    //                                      e = 0/8 (punch b)               =8 ->  punch availability table OR punch 5/CD card 
    //                                                                             if word1 start by 01 is 5/CD card
    //                                                                             if word1 start by 00 is an availability card
    //                              
    // SOAPIIA 5 word per card (5/CD) punch format 
    //    |    word 1    |    word 2    |    word 3    |    word 4    |    word 5    |    word 6    |    word 7    |    word 8   |
    //    | 01 AAAA NNNN |     first    |    second    |    third     |    fourth    |    fifth     |   location of intructions  |
    //                   |  instruction | instruction  | instruction  | instruction  |  instruction |  1    2    3 |   4    5    |
    //      AAAA=ident                                                                              |  NNNN NNNN NN|NN NNNN NNNN |
    //      NNNN=card num
    //
    // SOAPIIA 5 word per card printout format 
    //    | 01 | AAAA | NNNN | word 1 | word 2 | word 3 | word 4 | word 5 | NNNN | NNNN | NNNN | NNNN | NNNN | 
    //                                                                      word1  word2 word3   word4  word5 location 
    //

    char loc[6], data_addr[6], inst_addr[6], OpCode[6], Data_Tag[6], Instr_Tag[6], rem1[6], rem2[6];
    char pch_word[20];
    t_int64 d, instr;
    int location, CardNum, ty;
    int b_non_blank, neg, b_blk_op, b_blk_i, b_blk_d, b_blk_l, b_800X, b_pch_b, b_pch_a, b_bypass, b_5cd; // punch control flags
    int i, sv_card_nbuf, n, NegZero;
    int pat1, pat2;

    word_to_ascii(loc,       1, 5, IOSync[0]);
    word_to_ascii(data_addr, 1, 5, IOSync[1]);
    word_to_ascii(inst_addr, 1, 5, IOSync[2]);
    word_to_ascii(OpCode,    1, 3, IOSync[3]);
    word_to_ascii(Data_Tag,  4, 1, IOSync[3]);
    word_to_ascii(Instr_Tag, 5, 1, IOSync[3]);
    word_to_ascii(rem1,      1, 5, IOSync[4]);
    word_to_ascii(rem2,      1, 5, IOSync[5]);
    instr    = IOSync[6];
    location = (int) ((IOSync[7] / D4) % D4);
    ty       = (int) ( IOSync[7]       % 10);
    CardNum  = (int) ( IOSync[8]       % D4);
    d        =  IOSync[9];
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

    // printf("bits %06d%04d%c ", printfw(IOSync[9]));    // to echo the control word of punched card

    if ((bMultiPass) && (b_pch_b) && (IOSync[0] / D8 == 01)) {
        b_5cd = 1; 
    } else {
        b_5cd = 0; 
    }

    if ((ty==1) || (ty==2)) b_pch_a=1; // card types 1 or 2 punch non generating code card

    // generate card
    if (b_pch_b) {
        // punch 5 words per card format or 
        // punch availability table (pat pseudo-op output)
        for(i=0;i<8;i++) {
            sprintf_word(pch_word, IOSync[i], 0, 1);
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
        }
        encode_char(ty == 0 ? ' ' : '0'+ty, 0); 
        encode_char(neg ? '-' : ' ', 0); 
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
        if (b_5cd) {
            // print 5 words per card format or 
            d = IOSync[0];
            pat1 = (int) ((d / D4) % D4);
            pat2 = (int) ( d       % D4);
            encode_lpt_num(01, 2);      // print 01
            encode_lpt_spc(1);
            encode_lpt_num(pat1, 4);    // print AAAA
            encode_lpt_spc(1);
            encode_lpt_num(pat2, 4);    // print NNNN
            encode_lpt_spc(1);
            for(i=1;i<=5;i++) {         // print 5 words as NNNNNNNNNs
                d = IOSync[i];
                NegZero = IOSync_NegativeZeroFlag[i];
                encode_lpt_word(d, NegZero, wf_NNNNNNNNNNs);
            }
            encode_lpt_spc(1);          // print locations of words as NNNN NNNN NNNN NNNN NNNN
            d = IOSync[6];
            for(i=1;i<=5;i++) {
                n = Shift_Digits(&d, 4); 
                if (i==3) {
                    d = IOSync[7];
                    n = n + Shift_Digits(&d, 2); 
                }
                encode_lpt_num(n, 4);    
                encode_lpt_spc(1);
            }
        } else {
            // print availability table (pat pseudo-op output)
            for(i=0; i<4; i++) {
                d = IOSync[i*2];
                pat1 = (int) ((d / D4) % D4);
                pat2 = (int) ( d       % D4);
                d = IOSync[i*2 + 1];
                encode_lpt_num(pat1, 4);
                encode_lpt_spc(2);
                encode_lpt_num(d, 10);
                encode_lpt_spc(2);
                encode_lpt_num(pat2, 4);
                encode_lpt_spc(5);
            }
        }
    } else if ((ty == 1) || (ty == 5)) {
        // print comment for card type 1 (SOAP II) or type 5 (SOAP modified for IT)
        encode_char(0, '0' + ty);
        encode_lpt_spc(14);
        encode_lpt_str(loc); encode_lpt_str(OpCode); 
        encode_lpt_str(data_addr); encode_lpt_str(Data_Tag); 
        encode_lpt_str(inst_addr); encode_lpt_str(Instr_Tag); 
        encode_lpt_str(rem1); encode_lpt_str(rem2); 
    } else {
        if (ty == 0) {
            encode_lpt_spc(1);
        } else {
            encode_char(0, '0' + ty);
        }
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

void encode_supersoap_wiring() 
{
    // encode soap card simulating soap control panel wiring for 533 
    // storage in output block (one card format)
    //                +-------------------+ 
    //    Word 9040:  | <-  Location   -> | Alphabetic
    //         9041:  | <-  Data Addr  -> | Alphabetic
    //         9042:  | <-  Inst Addr  -> | Alphabetic
    //                +-+-+-|-+-+-|-+-|-+-|
    //         9043:  |   Op Code |DTg|ITg| Alphabetic
    //                +-+-+-|-+-+-|-+-|-+-|
    //         9044:  | <- Remarks     -> | Alphabetic
    //         9045:  | <- Remarks     -> | Alphabetic
    //         9046:  |<-Assembled Instr->|
    //                +-+-|-+-+-+-|-+-+-|-|
    //         9047:  |   |N N N N|     |T| N N N N=Location, T=Type (0 if Blank)
    //         9048:  |  n n n n  |N N N N| N N N N=Card Number, n n n n = location2
    //         9049:  |a| | |d|e| |g| | |j| punch control word
    //                                      a =8 -> bank LOC OP etc, =0 -> punch  LOC2 LOC1 OP etc =7 -> PAT card
    //                                      b 
    //                                      c =8 -> 8 words
    //                                      d =8 -> five words per card
    //                                      e =9 -> positive, =8 -> negative
    //                                      f 
    //                                      g =8 -> ???
    //                                      h 
    //                                      i 
    //                                      j =4 -> punch 8004
    //    
    // SOAP printout format
    //    | Sg |    Location    |  OpCode  |   Data Addr    | Tg |  Instr Addr    | Tg | Remarks  | Drum Addr | NN NNNN NNNN[-] (signed word value at this drum addr)
    // SOAP punch format (load card, 1 word per card)
    // simulates punching over prepunched 1-word load card
    //    |    word1   |       nnnn |  24 addr 800? | NNNNNNNNNN | source soap line
    //    nnnn=card number
    //    addr=drum address where the word is loaded
    //    NNNNNNNNNN=word to be loaded at addr, with sign in last digit
    //
    // SuperSoap five word per card (FIV) punch format 
    //    |    word 1    |    word 2    |    word 3    |    word 4    |    word 5    |    word 6    |    word 7    |    word 8   |
    //    | 888888  NNNN |     fifth    |    fourth    |    third     |    second    |    first     |   location of intructions  |
    //                   |  instruction | instruction  | instruction  | instruction  |  instruction |  5    4    3 |   2    1    |
    //     NNNN=card num                                                                            |  NNNN NNNN NN|NN NNNN NNNN |
    //      
    //
    // SuperSoap five word per card printout format 
    //    | 88888 | NNNN | word 5 | word 4 | word 3 | word 2 | word 1 | NNNN | NNNN | NNNN | NNNN | NNNN | 
    //                                                                  word5  word4 word3   word2  word1 location 
    //

    char loc[6], data_addr[6], inst_addr[6], OpCode[6], Data_Tag[6], Instr_Tag[6], rem1[6], rem2[6];
    char pch_word[20];
    t_int64 d, instr;
    int location, location2, CardNum, ty, opcodeNum;
    int b_blank, neg, b4, fiv, b_8word; // punch control flags
    int i, sv_card_nbuf, n, NegZero;
    int pat1, pat2;
    char cardtype;

    word_to_ascii(loc,       1, 5, IOSync[0]);
    word_to_ascii(data_addr, 1, 5, IOSync[1]);
    word_to_ascii(inst_addr, 1, 5, IOSync[2]);
    word_to_ascii(OpCode,    1, 3, IOSync[3]);
    word_to_ascii(Data_Tag,  4, 1, IOSync[3]);
    word_to_ascii(Instr_Tag, 5, 1, IOSync[3]);
    word_to_ascii(rem1,      1, 5, IOSync[4]);
    word_to_ascii(rem2,      1, 5, IOSync[5]);
    instr     = IOSync[6];
    location  = (int) ((IOSync[7] / D4) % D4);
    ty        = (int) ( IOSync[7]       % 10);
    CardNum   = (int) ( IOSync[8]       % D4);
    location2 = (int) ( (IOSync[8] / (10*D4)) % D4);
    d         =  IOSync[9];

    b4      = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    i       = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    i       = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    i       = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    i       = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    neg     = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    fiv     = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_8word = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    i       = ((int) (d % 10) == 8) ? 1:0; d = d / 10;    
    b_blank = (int) (d % 10); d = d / 10;

    opcodeNum = (int) (IOSync[3] / D4); // origina ibm650 char opcode

    if (b_blank==7) {
        cardtype = 'P'; // punch availability table card PAT
    } else if (fiv) {
        cardtype = '5'; // punch five words per card
    } else if (b_8word) {
        cardtype = '8'; // punch 8-words load binary card
    } else if ((b_blank) || (ty==1) || (ty==3)) {
        //XXX missing PAL output, 
        cardtype = 'A'; // comment card
    } else if ((ty==2) || (ty==4) || ((location >= 8000) && (location <= 8009))) {
        cardtype = 'B'; // 800X card
    } else {
        //XXX missing  PLR, FIL in one-per-card form, FIL in five-per-card form, DEK
        cardtype = 'C'; // regular code card
    }

    // generate card
    if (cardtype=='P') {
        // punch availability table (pat pseudo-op output)
        for(i=0;i<8;i++) {
            sprintf_word(pch_word, IOSync[i], 0, 1);
            encode_pch_str(pch_word);     
        }
    } else if (cardtype=='8') {
        // punch 8-words load binary card
        for(i=0;i<8;i++) {
            d = IOSync[i];
            NegZero = IOSync_NegativeZeroFlag[i];
            sprintf_word(pch_word, d, NegZero, 1);
            encode_pch_str(pch_word);   
        }
    } else if (cardtype=='5') {
        // punch five-per-card per card format 
        sprintf(pch_word, "888888%04d", (int)(IOSync[8] % D4)); // punch six 8's, then the card number
        encode_pch_str(pch_word);    
        for(i=1;i<6;i++) {
            sprintf_word(pch_word, IOSync[i], 0, 1); // sign on units
            encode_pch_str(pch_word);     
        }
        sprintf_word(pch_word, IOSync[6], 0, 0); // locations -> no sign
        encode_pch_str(pch_word);     
        sprintf_word(pch_word, IOSync[7], 0, 0);
        encode_pch_str(pch_word);     
    } else {
        // cardtype A, B or C
        if (cardtype=='A') {
            encode_pch_str("?000008000");  // punch non generating code card
        } else if (cardtype=='B') {
            encode_pch_str("F919548000");  // punch for 800X locations
        } else {
            encode_pch_str("F919541953"); // punch for load card
        }
        if ((ty!=1) && (ty!=3) && ((opcodeNum==647963) || (opcodeNum==637664))) {
            sprintf(pch_word, " %s%04d", loc, CardNum);  // card DRC or COD
        } else {
            sprintf(pch_word, "      %04d", CardNum);    // consecutive card count
        }
        encode_pch_str(pch_word);  
        if   (cardtype=='A') {
            encode_pch_str("          ");
            encode_pch_str("          ");
        } else {
            sprintf(pch_word, "24%04d800?", location);// addr to place the loaded word
            encode_pch_str(pch_word);    
            sprintf_word(pch_word, AbsWord(instr) * (neg ? -1:1), ((neg) && (instr == 0)) ? 1:0, 1);
            encode_pch_str(pch_word);    
        }
        // input reproduced
        encode_char(ty == 0 ? ' ' : '0'+ty, 0); 
        encode_char(neg ? '-' : ' ', 0); 
        sv_card_nbuf = card_nbuf;  // save pch bufer current pos
        encode_pch_str(loc);       encode_pch_str(OpCode); 
        encode_pch_str(data_addr); encode_pch_str(Data_Tag); 
        encode_pch_str(inst_addr); encode_pch_str(Instr_Tag); 
        encode_pch_str(rem1);      encode_pch_str(rem2); 
        // convert to lowercase for punching
        for (i=sv_card_nbuf;i<card_nbuf;i++) 
            if ((card_buf[i] >= 'A') && (card_buf[i] <= 'Z')) 
                card_buf[i] = card_buf[i] - 'A' + 'a';
    }
    card_buf[card_nbuf] = 0;

    // generate printout
    if (cardtype=='5') {
        // print five words per card format 
        encode_lpt_str("888888 ");
        encode_lpt_num((int)(IOSync[8] % D4), 4); // card number
        encode_lpt_spc(1);
        for(i=1;i<=5;i++) {         // print 5 words as NNNNNNNNNs
            d = IOSync[i];
            NegZero = IOSync_NegativeZeroFlag[i];
            encode_lpt_word(d, NegZero, wf_NNNNNNNNNNs);
        }
        encode_lpt_spc(1);          // print locations of words as NNNN NNNN NNNN NNNN NNNN
        d = IOSync[6];
        for(i=1;i<=5;i++) {
            n = Shift_Digits(&d, 4); 
            if (i==3) {
                 d = IOSync[7];
                 n = n + Shift_Digits(&d, 2); 
            }
            encode_lpt_num(n, 4);    
            encode_lpt_spc(1);
        }
    } else if (cardtype=='8') {
        // punch 8-words load binary card
        // print out card contents 8 words in format NN NNNN NNNN+
        for(i=0;i<8;i++) {
           d = IOSync[i];
           NegZero = IOSync_NegativeZeroFlag[i];
           encode_lpt_word(d, NegZero, wf_sNNNNNNNNNN);
           encode_lpt_spc(2);
        }
    } else if (cardtype=='P') {
        // print availability table (pat pseudo-op output)
        for(i=0; i<4; i++) {
            d = IOSync[i*2];
            pat1 = (int) ((d / D4) % D4);
            pat2 = (int) ( d       % D4);
            d = IOSync[i*2 + 1];
            encode_lpt_num(pat1, 4);
            encode_lpt_spc(2);
            encode_lpt_num(d, 10);
            encode_lpt_spc(2);
            encode_lpt_num(pat2, 4);
            encode_lpt_spc(5);
        }
    } else {
        encode_lpt_num(CardNum, 4); 
        encode_lpt_spc(3);
        if (ty == 1) {
            // print comment card type 1 
            encode_lpt_str("1    ");
            encode_lpt_str(loc); encode_lpt_str(OpCode); 
            encode_lpt_str(data_addr); encode_lpt_str(Data_Tag); 
            encode_lpt_str(inst_addr); encode_lpt_str(Instr_Tag); 
            encode_lpt_str(rem1); encode_lpt_str(rem2); 
        } else {
            if (ty == 0) {
                encode_lpt_spc(1);
            } else {
                encode_char(0, '0' + ty);
            }
            encode_lpt_spc(2); encode_char(0, neg ? '-':' '); encode_lpt_spc(1);
            encode_lpt_str(loc); encode_lpt_spc(2); 
            encode_lpt_str(OpCode); encode_lpt_spc(2); 
            encode_lpt_str(data_addr); encode_lpt_str(Data_Tag); encode_lpt_spc(1); 
            encode_lpt_str(inst_addr); encode_lpt_str(Instr_Tag); encode_lpt_spc(3); 
            encode_lpt_str(rem1); encode_lpt_str(rem2); encode_lpt_spc(4); 
            if (b_blank) {
                // blank loc opcode data_addr instr_addr
            } else {
                if (location2!=location) {
                    encode_lpt_num(location2, 4);
                } else {
                    encode_lpt_spc(4); 
                }
                encode_lpt_spc(1); 
                encode_lpt_num(location, 4); encode_lpt_spc(2); 
                encode_char(0, neg ? '-':'+'); 
                d = instr;
                n = Shift_Digits(&d, 2); // operation code (2 digits)
                encode_lpt_num(n, 2); encode_lpt_spc(1);
                n = Shift_Digits(&d, 4); // data addr (4 digits)
                encode_lpt_num(n, 4); encode_lpt_spc(1); 
                n = Shift_Digits(&d, 4); // instr addr (4 digits)
                encode_lpt_num(n, 4); 
            }
        }
    }
}

void encode_is_wiring(void) 
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

    bSetHiPunch = (IOSync[0] < 0) ? 2 : 0; // first bSetHiPunch is 2 if word negative (signals a load card must be punched)

    loc        = (int) ((IOSync[0]   / D4) % D4);
    CardNum    = (int) ((IOSync[9] / D4) % D4);
    wc         = (int) ((IOSync[1] / D4) % D4);
    PrNum      = (int) ( IOSync[8]);
    bTraceCard = (IOSync[0] / D8) > 0 ? 1 : 0;   // if to higher digits are nonzero -> is a trace card

    if (bSetHiPunch) {
        // punch a load card
        for(i=0;i<8;i++) {
            d = IOSync[i];
            NegZero = IOSync_NegativeZeroFlag[i];
            if ((i==0) && (d < 0)) d = -d;         // get absolute value for IOSync[0]
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
               d = IOSync[i+2];
               NegZero = IOSync_NegativeZeroFlag[i+2];
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
            d = IOSync[i];
            NegZero = IOSync_NegativeZeroFlag[i];
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

void encode_it_wiring(void) 
{
    // encode card for IT compiler modified soap 
    // from IT manual at http://www.bitsavers.org/pdf/ibm/650/CarnegieInternalTranslator.pdf
    // storage in output block 
    //                +-------------------+ 
    //    Word 1977:  | <-  Loc. Label -> | Alphabetic
    //         1978:  | <-   Op Code   -> | Alphabetic
    //         1979:  | <-  Data Addr  -> | Alphabetic
    //         1980:  | <-  Inst Addr  -> | Alphabetic
    //         1981:  | <-   Remarks   -> | Alphabetic
    //         1982:  | <-   Remarks   -> | Alphabetic
    //                +-------------------+ 
    //         1983:  |                   | Not Used
    //         1984:  |                   | Not Used
    //                +-------------------+ 
    //         1985:  |   |N N N N|       | N N N N=Card Number
    //         1986:  |a|b|c|d|e|f|g|h|i|j| a = 0/8 =8 -> reservation card
    //                                      b = 0/8 (regional setting) =0 -> card type 3, =8 -> card type 4
    //                                      c = 0/8 
    //                                      d = 0/8 =8 -> negative value
    //                                      e = 0/8 
    //                                      f = 0/8 
    //                                      g = 0/8 =8 -> punching a PIT card
    //                                      h = 0/8 =8 -> type 1 data out format
    //                                      i = 0/8 
    //                                      j = 0/8 
    //                              
    // SIT printout format
    //    | Card Num | Ty |  Location  |  Sg  |  OpCode  |   Data Addr |  Instr Addr  |  Remarks  
    // SIT punch format is SOAP source card format
    //    Column:   41 | 42 | 43 44 45 46 47 | 48 49 50 | 51 52 53 54 55 | 56 | 57 58 59 60 61 | 62 | 63 64 65 66 67 68 69 70 71 72
    //              Ty | Sg |    Location    |  OpCode  |   Data Addr    |    |  Instr Addr    |    | Remarks
    //
    //    Ty = Type = blank, 3 or 4 (regional setting)
    //    Sg = sign = blank or -
    //
    // If word 1986 contains 8 in digit h, it is a type 1 data out card format
    //                +----+------+-------+ 
    //    Word 1977:  | VV | +NNN | SSSS  | IT variable 1
    //         1978:  |       Word        | 
    //                +-------------------+ 
    //         1979:  |                   | IT variable 2 (zero if none)
    //         1980:  |                   |
    //                +-------------------+ 
    //         1981:  |                   | IT variable 3
    //         1982:  |                   |
    //                +-------------------+ 
    //         1983:  |                   | IT variable 4
    //         1984:  |                   | 
    //                +-------------------+ 
    //         1985:  |                   | Not used
    //         1986:  |8|0|0|0|0|0|8|8|0|0| control word for type 1 data out card
    //
    //    VV = IT variable being punched: 01 -> I type, 02 -> Y type, 03 -> C type
    //    + N N N = variable number (I5 -> 01 0005). + means zoro with Y(12) overpunch
    //    S S S S = statement number of IT source program where TYPE command that generates the card is
    //    Word = value from IT variable. If type I, is an integer. If type C or Y
    //           type is word is float (M MMMMMMM EE -> M=mantisa, EE=exponent)
    //           can be is negative (X(11) overpunch in last digit)
    //    up to 4 pairs var-word per card
    //    leading zeroes of each word are replaced by spaces

    char pch_word[20];
    char loc[6], data_addr[6], inst_addr[6], OpCode[6], rem1[6], rem2[6];
    t_int64 d;
    int CardNum, ty;
    int b, neg, b_pit, b_reg, b_resv, b_data; // punch control flags
    int i;

    word_to_ascii(loc,       1, 5, IOSync[0]);
    word_to_ascii(OpCode,    1, 3, IOSync[1]);
    word_to_ascii(data_addr, 1, 5, IOSync[2]);
    word_to_ascii(inst_addr, 1, 5, IOSync[3]);
    word_to_ascii(rem1,      1, 5, IOSync[4]);
    word_to_ascii(rem2,      1, 5, IOSync[5]);
    CardNum  = (int) ((IOSync[8] / D4) % D4);
    d        =  IOSync[9];
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_data   = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_pit    = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    neg      = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_reg    = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_resv   = ((int) (d % 10) == 8) ? 1:0; d = d / 10;

    // printf("bits %06d%04d%c ", printfw(IOSync[9]));    // to echo the control word of punched card

    // generate card
    if (b_data) {
        // punch type 1 data out card
        for (i=0;i<4;i++) {
            sprintf_word(pch_word, IOSync[i*2+0], 0, (i==0) ? 3:0);    // punch variable name
            encode_pch_str(pch_word);   
            sprintf_word(pch_word, IOSync[i*2+1], 0, (i==0) ? 3:0);    // punch variable value
            encode_pch_str(pch_word);   
            if (IOSync[i*2+2] == 0) break;                             // if next word is zero, no more variables to punch
        }
    } else {
        // punch SOAP source instruction
        for(i=0;i<40;i++) encode_pch_str(" "); // leave 40 first columns blank
        if (b_resv) {
            if (b_reg) {
                ty = 4;
            } else {
                ty = 3;
            }
        } else {
            ty = 0;
        }
        encode_char(ty  == 0 ? ' ' : '0'+ty, 0); 
        encode_char(neg == 0 ? ' ' : '-',    0); 
        encode_pch_str(loc);
        encode_pch_str(OpCode);
        encode_pch_str(data_addr);
        encode_pch_str(" ");
        encode_pch_str(inst_addr);
        encode_pch_str(" ");
        encode_pch_str(rem1);
        encode_pch_str(rem2);
        // convert to lowercase for punching
        for (i=40;i<card_nbuf;i++) 
            if ((card_buf[i] >= 'A') && (card_buf[i] <= 'Z')) 
                card_buf[i] = card_buf[i] - 'A' + 'a';
        card_buf[card_nbuf] = 0;
    }

    // generate printout
    if (b_data) {
        // print type 1 data out card. replace leading zeroes by spaces on each word
        for (i=0;i<4;i++) {
            encode_lpt_word(IOSync[i*2+0], 0, wf_nnnnnnnnnNs); // print variable name
            encode_lpt_spc(1);
            encode_lpt_word(IOSync[i*2+1], 0, wf_nnnnnnnnnNs); // print variable value
            encode_lpt_spc(1);
            if (IOSync[i*2+2] == 0) break;                             // if next word is zero, no more variables to punch
        }
    } else {
        // print generated soap source listing
        encode_lpt_spc(2);
        encode_lpt_num(CardNum, -4); 
        encode_lpt_spc(2);
        encode_char(0, ty  == 0 ? ' ' : '0'+ty); 
        encode_lpt_spc(2);
        encode_lpt_str(loc); 
        encode_lpt_spc(2); encode_char(0, neg ? '-':' '); encode_lpt_spc(1);
        encode_lpt_str(OpCode); encode_lpt_spc(3); 
        encode_lpt_str(data_addr); encode_lpt_spc(1); encode_lpt_spc(2); 
        encode_lpt_str(inst_addr); encode_lpt_spc(6); 
        encode_lpt_str(rem1); encode_lpt_str(rem2); 
    }
}

void encode_ra_wiring(void) 
{
    // encode card for Missile Systems Division, Lockheed Aircraft Corporation
    // regional assembly card - five load cards
    // storage in output block 
    //                +-------------------+ 
    //    Word 0977:  | XX AAAA XXXX      | Address A1 (X=don't care)
    //         0978:  | NN NNNN NNNN      | word 1
    //         0979:  | XX AAAA XXXX      | Address A2
    //         0980:  | NN NNNN NNNN      | word 2
    //         0981:  | XX AAAA XXXX      | Address A3
    //         0982:  | NN NNNN NNNN      | word 3
    //         0983:  | XX AAAA XXXX      | Address A4
    //         0984:  | NN NNNN NNNN      | word 4
    //         0985:  | XX AAAA XXXX      | Address A5
    //         0986:  | NN NNNN NNNN      | word 5
    //                +-------------------+
    //     
    // punch card format 
    //
    //    Column: | 1 2 3 4 - 10 | 11 - 14 | 15 16 | 17 - 20 | 21 - 24 | 25 - 28 | 29 30 | 31 - 34 | 35 - 38 | 39 - 42 | 43 44 | 45 - 48 | 49 - 52 | 53 - 56 | 57 58 | 59 - 62 | 63 - 66 | 67 - 70 | 71 72 | 73 - 76 | 77 - 80 |
    //            |     +        | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | N N N N | N  N  | N N N N | N N N N | 
    //                           | Addr    | Op    | Data    | Instr   | Addr    | Op    | Data    | Instr   | Addr    | Op    | Data    | Instr   | Addr    | Op    | Data    | Instr   | Addr    | Op    | Data    | Instr   | 
    //                           | Location| Code  | Addr    | Addr    | Location| Code  | Addr    | Addr    | Location| Code  | Addr    | Addr    | Location| Code  | Addr    | Addr    | Location| Code  | Addr    | Addr    | 
    //                           |   (A1)     (O1)    (D1)     (I1)    |   (A2)     (O2)    (D2)     (I2)    |   (A3)     (O3)    (D3)     (I3)    |   (A4)     (O4)    (D4)     (I4)    |   (A5)     (O5)    (D5)     (I5)    | 
    //                           |               Word 1                |               Word 2                |               Word 3                |               Word 4                |               Word 5                | 
    //                         
    // printout of five load card (only prints words 1, 2 and 3)
    //
    //    Column: | 1   2 | 3 - 6   | 7 8 | 9 10 | 11 | 12 - 15 | 16 | 17 - 20 | 21 | 22 - 25 | 26 - 29 | 30 31 | 32 33 | 34 | 35 - 38 | 39 | 40 - 43 | 44 | 45 - 48 | 49 - 52 | 53 54 | 55 56 | 57 | 58 - 61 | 62 | 63 - 66 | 67 |
    //            |       | N N N N |     | N  N |    | N N N N |    | N N N N | s  |         | N N N N |       | N  N  |    | N N N N |    | N N N N | s  |         | N N N N |       | N  N  |    | N N N N |    | N N N N | s  |
    //                    | Addr    |     | Op   |    | Data    |    | Instr   | sign         | Addr    |       | Op    |    | Data    |    | Instr   | sign         | Addr    |       | Op    |    | Data    |    | Instr   | sign
    //                    | Location|     | Code |    | Addr    |    | Addr    |              | Location|       | Code  |    | Addr    |    | Addr    |              | Location|       | Code  |    | Addr    |    | Addr    |
    //                    |   (A1)        | (O1) |    | (D1)    |    | (I1)    |              |  (A2)           | (O2)  |    | (D2)    |    | (I2)    |              |  (A3)           | (O3)  |    | (D3)    |    | (I3)    |
    //                    |                    Word 1                          |              |                       Word 2                          |              |                       Word 3                          |
   
    char pch_word[20];
    t_int64 d; 
    int n; 

    encode_pch_str("  +       "); 

    d = IOSync[0]; Shift_Digits(&d, 2); n=Shift_Digits(&d, 4);
    sprintf_word(pch_word,     n, 0, 0);    // A1
    encode_pch_str(&pch_word[6]);   
    sprintf_word(pch_word,     IOSync[1], 0, 0);    // word 1
    encode_pch_str(pch_word);   

    d = IOSync[2]; Shift_Digits(&d, 2); n=Shift_Digits(&d, 4);
    sprintf_word(pch_word,     n, 0, 0);    // A2
    encode_pch_str(&pch_word[6]);   
    sprintf_word(pch_word,     IOSync[3], 0, 0);    // word 2
    encode_pch_str(pch_word);   

    d = IOSync[4]; Shift_Digits(&d, 2); n=Shift_Digits(&d, 4);
    sprintf_word(pch_word,     n, 0, 0);    // A3
    encode_pch_str(&pch_word[6]);   
    sprintf_word(pch_word,     IOSync[5], 0, 0);    // word 3
    encode_pch_str(pch_word);   

    d = IOSync[6]; Shift_Digits(&d, 2); n=Shift_Digits(&d, 4);
    sprintf_word(pch_word,     n, 0, 0);    // A4
    encode_pch_str(&pch_word[6]);   
    sprintf_word(pch_word,     IOSync[7], 0, 0);    // word 4
    encode_pch_str(pch_word);   

    d = IOSync[8]; Shift_Digits(&d, 2); n=Shift_Digits(&d, 4);
    sprintf_word(pch_word,     n, 0, 0);    // A5
    encode_pch_str(&pch_word[6]);   
    sprintf_word(pch_word,     IOSync[9], 0, 0);    // word 5
    encode_pch_str(pch_word);   

    encode_lpt_str("  ");

    d = IOSync[0];
    Shift_Digits(&d, 2); n = Shift_Digits(&d, 4);
    encode_lpt_num(n, 4); 
    encode_lpt_spc(2);
    d = IOSync[1];
    encode_lpt_word(d, 0, wf_NN_NNNN_NNNNs); 
    encode_lpt_spc(4);
    
    d = IOSync[2];
    Shift_Digits(&d, 2); n = Shift_Digits(&d, 4);
    d = IOSync[3];
    if ((n==0) && (d==0)) {
       encode_lpt_spc(4+2+13+4);
    } else {
       encode_lpt_num(n, 4); 
       encode_lpt_spc(2);
       encode_lpt_word(d, 0, wf_NN_NNNN_NNNNs); 
       encode_lpt_spc(4);
    }

    d = IOSync[4];
    Shift_Digits(&d, 2); n = Shift_Digits(&d, 4);
    d = IOSync[5];
    if ((n==0) && (d==0)) {
       encode_lpt_spc(4+2+13+4);
    } else {
       encode_lpt_num(n, 4); 
       encode_lpt_spc(2);
       encode_lpt_word(d, 0, wf_NN_NNNN_NNNNs); 
       encode_lpt_spc(4);
    }
}

void encode_fortransit_wiring(void) 
{
    // encode card for FORTRANSIT modified IT compiler
    // from FORTRANSIT manual at http://bitsavers.org/pdf/ibm/650/28-4028_FOR_TRANSIT.pdf
    // implemented Fortransit II (S) 
    // word 1986 (control word) specifies what is being punched)
    // storage in output block 
    //                +-------------------+ 
    //    Word 1977:  | <-  statement  -> | Alphabetic
    //         1978:  | <-  statement  -> | Alphabetic
    //         1979:  | <-  statement  -> | Alphabetic
    //         1980:  | <-  statement  -> | Alphabetic
    //         1981:  | <-  statement  -> | Alphabetic
    //         1982:  | <-  statement  -> | Alphabetic
    //                +-------------------+ 
    //         1983:  |                   | Not Used
    //         1984:  |                   | Not Used
    //                +-----------+-------+ 
    //         1985:  |           |N N N N| N N N N=Statement Number
    //         1986:  |a|b|c|d|e|f|g|h|i|j| Control Word
    //                                      a = 0/8 =8 -> punch a data card 
    //                                      b = 0/8 
    //                                      c = 0/8 
    //                                      d = 0/8 =8 -> ???
    //                                      e = 0/8 
    //                                      f = 0/8 
    //                                      g = 0/8 =8 -> punching a IT source card, =0 -> punching SOAP card 
    //                                      h = 0/8 
    //                                      i = 0/8 =8 -> punching a FORTRANSIT card 
    //                                      j = 0/8 =8 -> punching an IT header card (8 word load card format)
    //                              
    // IT card punch format
    //    Column:  1  2  3  4 |   5   | 6 - 42 |  43 - 70  | 71 72 |  73 - 80  |
    //               N N N N  |   +   |        | Statement |       | Statement |
    //              Statement | Y(12) |        |  max 28   |       | number as |
    //                Number  | Punch |        |  chars    |       | comment   |        
    //
    //
    // SOAP card storage in output block 
    //                +-------------------+ 
    //    Word 1977:  | <-  Loc. Label -> | Alphabetic
    //         1978:  | <-  Data Addr  -> | Alphabetic
    //         1979:  | <-  Inst Addr  -> | Alphabetic
    //         1980:  | <-   Op Code   -> | Alphabetic
    //         1981:  | <-   Remarks   -> | Alphabetic
    //         1982:  | <-   Remarks   -> | Alphabetic
    //                +-------------------+ 
    //         1983:  |                   | Not Used
    //         1984:  |                   | Not Used
    //                +-------------------+ 
    //         1985:  |           |N N N N| N N N N=Card Number as defined above
    //         1986:  | <- Control Word-> | As defined above

    char pch_word[20];
    char lin[31];
    char loc[6], data_addr[6], inst_addr[6], OpCode[6], rem1[6], rem2[6];
    t_int64 d;
    int CardNum;
    int b, neg, b_it_hdr, b_it_src, b_fort, b_soap, b_data; // punch control word flags
    int i;

    word_to_ascii(&lin[0],   1, 5, IOSync[0]);
    word_to_ascii(&lin[5],   1, 5, IOSync[1]);
    word_to_ascii(&lin[10],  1, 5, IOSync[2]);
    word_to_ascii(&lin[15],  1, 5, IOSync[3]);
    word_to_ascii(&lin[20],  1, 5, IOSync[4]);
    word_to_ascii(&lin[25],  1, 5, IOSync[5]);
    lin[30] = 0;

    CardNum  = (int) (IOSync[8] % D4);

    word_to_ascii(loc,       1, 5, IOSync[0]);
    word_to_ascii(data_addr, 1, 5, IOSync[1]);
    word_to_ascii(inst_addr, 1, 5, IOSync[2]);
    word_to_ascii(OpCode,    1, 3, IOSync[3]);
    word_to_ascii(rem1,      1, 5, IOSync[4]);
    word_to_ascii(rem2,      1, 5, IOSync[5]);

    neg = 0;

    d        = IOSync[9];
    b_it_hdr = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_fort   = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_it_src = ((int) (d % 10) == 8) ? 1:0; d = d / 10; b_soap = ((b_fort == 1) && (b_it_src == 0));
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b        = ((int) (d % 10) == 8) ? 1:0; d = d / 10;
    b_data   = ((int) (d % 10) == 8) ? 1:0; d = d / 10;

    // printf("bits %06d%04d%c ", printfw(IOSync[9]));    // to echo the control word of punched card
    // generate card
    if (b_data) {
        // punch data card output for PUNCH fortransit command
        for (i=0;i<8;i++) {
            sprintf_word(pch_word, IOSync[i], 0, 0);    
            encode_pch_str(pch_word);   
        }
    } else if (b_it_hdr) {
        // punch IT header card as 8 word per card load card format
        for (i=0;i<8;i++) {
            sprintf_word(pch_word, IOSync[i], 0, 1);    
            encode_pch_str(pch_word);   
        }
    } else if (b_soap) {
        // punch SOAP source instruction
        for(i=0;i<40;i++) encode_pch_str(" "); // leave 40 first columns blank
        encode_pch_str(" ");
        encode_char(neg == 0 ? ' ' : '-',    0); 
        encode_pch_str(loc);
        encode_pch_str(OpCode);
        encode_pch_str(data_addr);
        encode_pch_str(" ");
        encode_pch_str(inst_addr);
        encode_pch_str(" ");
        encode_pch_str(rem1);
        encode_pch_str(rem2);
        // convert to lowercase for punching
        for (i=40;i<card_nbuf;i++) 
            if ((card_buf[i] >= 'A') && (card_buf[i] <= 'Z')) 
                card_buf[i] = card_buf[i] - 'A' + 'a';
    } else if (b_it_src) {
        // punch IT source card 
        sprintf_word(pch_word, CardNum, 0, 0);          // punch statement number
        for (i=0;i<4;i++) pch_word[i] = pch_word[i+6];  
        pch_word[4] = '+';
        for (i=5;i<10;i++) pch_word[i] = ' ';           // punch separation spaces
        encode_pch_str(pch_word);   
        for (i=10;i<42;i++) encode_pch_str(" ");
        encode_pch_str(lin);                            // punch statement
        encode_pch_str("    ");                            
        sprintf_word(pch_word, CardNum, 0, 0);          // punch statement number again as comment
        for (i=0;i<4;i++) pch_word[i] = pch_word[i+6];  
        pch_word[4] = 0;
        encode_pch_str(pch_word);   
        // convert to lowercase for punching
        for (i=0;i<card_nbuf;i++) 
            if ((card_buf[i] >= 'A') && (card_buf[i] <= 'Z')) 
                card_buf[i] = card_buf[i] - 'A' + 'a';
    }

    // generate printout
    if (b_data) {
        // print data card output for PUNCH fortransit command
        for (i=0;i<8;i++) {
            d = IOSync[i];
            if ((d == 0) && (i != 0)) {
                encode_lpt_spc(11);
            } else {
                encode_lpt_word(d, 0, wf_nnnnnnnnnNs); 
            }
            encode_lpt_spc(1);
        }
    } else if (b_it_hdr) {
        // print IT header card as 8 word per card load card format
        for (i=0;i<8;i++) {
            if (i==4) {
                encode_lpt_word(IOSync[i], 0, wf_NNNNNNNNNN); 
            } else {
                encode_lpt_word(IOSync[i], 0, wf_nnnnnnnnnH); 
            }
        }
    } else if (b_soap) {
        // print generated SOAP source listing
        encode_lpt_spc(2);
        encode_lpt_num(CardNum, -4); 
        encode_lpt_spc(6);
        encode_lpt_str(loc);  
        encode_lpt_spc(2); encode_char(0, neg ? '-':' '); encode_lpt_spc(1);
        encode_lpt_str(OpCode); encode_lpt_spc(3); 
        encode_lpt_str(data_addr); encode_lpt_spc(3); 
        encode_lpt_str(inst_addr); encode_lpt_spc(6); 
        encode_lpt_str(rem1); encode_lpt_str(rem2); 
    } else if (b_it_src) {
        // print generated it source listing
        if (CardNum == 0) {
            encode_lpt_spc(5);
        } else {
            encode_lpt_num(CardNum, -4); 
            encode_lpt_str("+");
        }
        encode_lpt_spc(37);
        encode_lpt_str(lin); 
        encode_lpt_spc(4);
        encode_lpt_num(CardNum, 4); 
    }
}


/* Card punch routine */
uint32 cdp_cmd(UNIT * uptr, uint16 cmd, uint16 addr)
{
    int i,c,h;
    uint16 image[80];
    uint32              wiring;

    /* Are we currently tranfering? */
    if (uptr->u5 & URCSTA_BUSY)
        return SCPE_BUSY;

    /* Test ready */
    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_EXP, &cdp_dev, "No cards (no file attached)\n");
        return SCPE_NOCARDS;
    }

    // copy and translate drum memory words to chars to punch 
    // using the control panel wiring. 

    wiring = (uptr->flags & UNIT_CARD_WIRING);
    card_nbuf = card_nlpt = 0;

    if (wiring == WIRING_SOAP) {
        // encode soap card simulating soap control panel wiring for 533 (gasp!)
        encode_soap_wiring(0);
    } else if (wiring == WIRING_SOAPA) {
        // encode soap card for multipass sopa IIA
        encode_soap_wiring(1);
    } else if (wiring == WIRING_SUPERSOAP) {
        // encode super soap card 
        encode_supersoap_wiring();
    } else if (wiring == WIRING_IS) {
        // encode floating point interpretive system (bell interpreter) card 
        encode_is_wiring();
    } else if (wiring == WIRING_IT) {
        // encode Carnegie Internal Translator compiler card 
        encode_it_wiring();
    } else if (wiring == WIRING_RA) {
        // endecode Missile Systems Division Lockheed Aircraft Corporation - regional assembly card
        encode_ra_wiring();
    } else if (wiring == WIRING_FORTRANSIT) {
        // encode Fortransit translator card 
        encode_fortransit_wiring();
    } else if (wiring == WIRING_8WORD) {
        // encode 8 words per card
        encode_8word_wiring();
    } else {
        // default wiring: decode up to 8 numerical words per card
        encode_8word_wiring();
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
                sim_fwrite(card_lpt, 1, card_nlpt, cdp_unit[0].fileref);
            }
        }
    }

    // trim right spaces for printing punch card
    card_buf[card_nbuf] = 0;
    sim_debug(DEBUG_DETAIL, &cpu_dev, "Punch Card: %s\n", card_buf);

    /* punch the cards */
    for (i=0; i<80; i++) {
        if (i >= card_nbuf) {
            c = 32;
        } else {
            c = card_buf[i];
        }
        if (c == 32) {
            // no punch
            image[i] = 0;
        } else {
            // punch char
            h = sim_ascii_to_hol(c);
            image[i] = h;
        }
    }
    sim_punch_card(uptr, image);
    sim_debug(DEBUG_CMD, &cdp_dev, "PUNCH\n");
    uptr->u5 |= URCSTA_BUSY;
    uptr->u6++; // incr number of punched cards 

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

    r = sim_card_attach(uptr, file);
    if (SCPE_BARE_STATUS(r) != SCPE_OK)
       return r;
    uptr->u5 = 0;
    uptr->u6 = 0; // u6 = number of cards punched

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


