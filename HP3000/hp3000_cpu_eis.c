/* hp3000_cpu_eis.c: HP 30012A Extended Instruction Set simulator

   Copyright (c) 2020, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   29-Sep-20    JDB     Passes the EIS decimal arithmetic firmware diagnostic (D431)
   14-Sep-20    JDB     Passes the EIS floating point firmware diagnostic (D431)
   05-Sep-20    JDB     Created

   References:
     - HP 3000 Series II/III System Reference Manual
         (30000-90020, July 1978)
     - Machine Instruction Set Reference Manual
         (30000-90022, June 1984)
     - HP 3000 Series II System Microprogram Listing
         (30000-90023, August 1976)


   This module implements the HP 30012A Extended Instruction Set firmware
   consisting of extended floating point and decimal arithmetic instructions.
   The set contains these instructions:

     Name  Description
     ----  ------------------------------
     EADD  Extended precision add
     ESUB  Extended precision subtract
     EMPY  Extended precision multiply
     EDIV  Extended precision divide
     ENEG  Extended precision negate
     ECMP  Extended precision compare

     ADDD  Add decimal
     CMPD  Compare decimal
     CVAD  Convert ASCII to decimal
     CVBD  Convert binary to decimal
     CVDA  Convert decimal to ASCII
     CVDB  Convert decimal to binary
     DMPY  Double logical multiply
     MPYD  Multiply decimal
     NSLD  Normalizing shift left decimal
     SLD   Shift left decimal
     SRD   Shift right decimal
     SUBD  Subtract decimal

   The floating-point instructions occupy the the firmware extension range
   020400-020417.  For each instruction, addresses of the operand(s) and result
   as DB+ relative word offsets reside on the stack.  They are encoded as
   follows:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   0 | 1   0   0   0 |  EADD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Add the four-word floating-point number addressed by RA to the four-word
   floating-point number addressed by RB and store the result in the four-word
   target area addressed by RC.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   0 | 1   0   0   1 |  ESUB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Subtract the four-word floating-point number addressed by RA from the
   four-word floating-point number addressed by RB and store the result in the
   four-word target area addressed by RC.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   0 | 1   0   1   0 |  EMPY
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Multiply the four-word floating-point number addressed by RA to the four-word
   floating-point number addressed by RB and store the result in the four-word
   target area addressed by RC.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   0 | 1   0   1   1 |  EDIV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Divide the four-word floating-point number addressed by RA into the four-word
   floating-point number addressed by RB and store the result in the four-word
   target area addressed by RC.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   0 | 1   1   0   0 |  ENEG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Negate in place the four-word floating-point number addressed by RA.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   0 | 1   1   0   1 |  ECMP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Compare the four-word floating-point number addressed by RB to the four-word
   floating-point number addressed by RA and set the condition code
   appropriately.



   The decimal arithmetic instructions occupy the the firmware extension range
   020600-020777.  For most instructions, addresses of the source and target
   operands as DB+ relative byte (for packed decimal) or word (for binary)
   offsets reside on the stack.  They are encoded as follows:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0   0   0 | 0   0   0   1 |  DMPY
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Multiply the double-word unsigned integer contained in RB and RA to the
   double-word unsigned integer contained in RD and RC and leaves the four-word
   unsigned integer product on the stack.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0   0 | S | 0   0   1   0 |  CVAD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       0 = delete 2 words
       1 = delete 4 words

   Convert the external decimal number designated by RA (count) and RB (address)
   to a packed decimal number designated by RC (count) and RD (address).


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1 |  sign | S | 0   0   1   1 |  CVDA
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       0 = delete 1 word
       1 = delete 3 words

     Sign Control:

       00 = target sign is source sign
       01 = target sign is negative if source negative else unsigned
       10 = target sign is unsigned
       11 = target sign is unsigned

   Convert the packed decimal number designated by RA (address) to an external
   decimal number designated by RB (count) and RC (address).  The number of
   digits converted is also designated by RB.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0   0 | S | 0   1   0   0 |  CVBD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       0 = delete 2 words
       1 = delete 4 words

   Convert the binary number designated by RA (count) and RB (address) to a
   packed decimal number designated by RC (count) and RD (address).


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0   0 | S | 0   1   0   1 |  CVDB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       0 = delete 2 words
       1 = delete 3 words

   Convert the packed decimal number designated by RA (count) and RB (address)
   to a binary number designated by RC (address).  The number of target words is
   determined by the source digit count.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0 | sdec  | 0   1   1   0 |  SLD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       00 = delete no words
       01 = delete 2 words
       10 = delete 4 words

   Shift the packed decimal number designated by RA (count) and RB (address)
   left by the number of digits specified by the X register and store the result
   in a packed decimal number designated by RC (count) and RD (address).  Digits
   shifted off the end of the number are lost.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0 | sdec  | 0   1   1   1 |  NSLD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       00 = delete no words
       01 = delete 2 words
       10 = delete 4 words

   Shift the packed decimal number designated by RA (count) and RB (address)
   left by the number of digits specified by the X register and store the result
   in a packed decimal number designated by RC (count) and RD (address).  If
   shifting would lose significant digits off the end of the number, the shift
   count is reduced to leave the most-significant digit at the start of the
   packed number.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0 | sdec  | 1   0   0   0 |  SRD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       00 = delete no words
       01 = delete 2 words
       10 = delete 4 words

   Shift the packed decimal number designated by RA (count) and RB (address)
   right by the number of digits specified by the X register and store the
   result in a packed decimal number designated by RC (count) and RD (address).
   Digits shifted off the end of the number are lost.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0 | sdec  | 1   0   0   1 |  ADDD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       00 = delete no words
       01 = delete 2 words
       10 = delete 4 words

   Add the packed decimal number designated by RA (count) and RB (address) to
   the packed decimal number designated by RC (count) and RD (address) and store
   the result in the target area addressed by RD.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0 | sdec  | 1   0   1   0 |  CMPD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       00 = delete no words
       01 = delete 2 words
       10 = delete 4 words

   Compare the packed decimal number designated by RA (count) and RB (address)
   to the packed decimal number designated by RC (count) and RD (address) and
   set the condition code appropriately.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0 | sdec  | 1   0   1   1 |  SUBD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       00 = delete no words
       01 = delete 2 words
       10 = delete 4 words

   Subtract the packed decimal number designated by RA (count) and RB (address)
   from the packed decimal number designated by RC (count) and RD (address) and
   store the result in the target area addressed by RD.


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1   0 | sdec  | 1   1   0   0 |  MPYD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     S-Decrement:

       00 = delete no words
       01 = delete 2 words
       10 = delete 4 words

   Multiply the packed decimal number designated by RA (count) and RB (address)
   by the packed decimal number designated by RC (count) and RD (address) and
   store the result in the target area addressed by RD.


   Packed decimal (also known as COMPUTATIONAL-3, BCD, and binary-coded decimal)
   numbers contain from 1 to 28 digits that are stored in pairs in successive
   memory bytes in this format:

       0   1   2   3   4   5   6   7
     +---+---+---+---+---+---+---+---+
     | unused/digit  |     digit     |
     +---+---+---+---+---+---+---+---+
     |     digit     |     digit     |
     +---+---+---+---+---+---+---+---+

     [...]

     +---+---+---+---+---+---+---+---+
     |     digit     |     digit     |
     +---+---+---+---+---+---+---+---+
     |     digit     |     sign      |
     +---+---+---+---+---+---+---+---+

   The sign is always located in the lower four bits of the final byte, so
   numbers with an even number of digits will not use the upper four bits of the
   first byte.  Digits are represented by four-bit values from 0-9 (i.e., in
   Binary-Coded Decimal or BCD), with the most-significant digit first and the
   least-significant digit last.  The sign is given by one of these encodings:

     1100 - the number is positive
     1101 - the number is negative
     1111 - the number is unsigned

   All other values are interpreted as meaning the number is positive; however,
   only one of the three values above is generated.

   Numbers may begin at an even or odd byte address, and the size of the number
   (in digits) may be even or odd, so there are four possible cases of packing
   the starting digits into 16-bit words:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15    addr/size
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |    unused     |     digit     |      ...      |      ...      |  even/even
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     digit     |     digit     |      ...      |      ...      |  even/odd
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |      ...      |      ...      |    unused     |     digit     |  odd/even
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |              ...              |     digit     |     digit     |  odd/odd
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Numbers always end with the sign in the lower half of the byte, so there are
   two possible cases of packing the ending digits into 16-bit words, depending
   on the total number of digits:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     digit     |     sign      |              ...              |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |      ...      |      ...      |     digit     |     sign      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   External decimal (also known as DISPLAY, numeric display, and ASCII) values
   contain contain from 1 to 28 digits that are stored as ASCII characters in
   successive memory bytes in this format:

       0   1   2   3   4   5   6   7
     +---+---+---+---+---+---+---+---+
     |             digit             |
     +---+---+---+---+---+---+---+---+
     |             digit             |
     +---+---+---+---+---+---+---+---+

     [...]

     +---+---+---+---+---+---+---+---+
     |             digit             |
     +---+---+---+---+---+---+---+---+
     |         digit and sign        |
     +---+---+---+---+---+---+---+---+

   The number begins with the most-significant digit.  The sign is combined with
   the least-significant digit in the final byte.  Each digit except the LSD
   must be in the ASCII range "0" through "9".  Leading blanks are allowed, and
   the entire number may be blank, but blanks within a number are not.  The
   least-signifiant digit and sign are represented by either:

     "0" and "1" through "9" for an unsigned number
     "{" and "A" through "I" for a positive number
     "}" and "J" through "R" for a negative number

   Numbers may begin at an even or odd byte address, and the size of the number
   (in digits) may be even or odd, so there are four possible cases of packing
   into 16-bit words:

     - the number completely fills the words
     - the number has an unused leading byte in the first word
     - the number has an unused trailing byte in the last word
     - the number has an unused byte at each end

   Any unused bytes are not part of the number and are not disturbed.


   Eight user traps may be taken by these instructions if the T bit is on in the
   status register:

     Parameter  Description
     ---------  ------------------------------------------------
      000010    Extended Precision Floating Point Overflow
      000011    Extended Precision Floating Point Underflow
      000012    Extended Precision Floating Point Divide by Zero
      000013    Decimal Overflow
      000014    Invalid ASCII Digit
      000015    Invalid Decimal Digit
      000016    Invalid Source Word Count
      000017    Invalid Decimal Length


   Implementation notes:

    1. Each instruction executor begins with a comment listing the instruction
       mnemonic and, following in parentheses, the condition code setting, or
       "none" if the condition code is not altered, and a list of any traps that
       might be generated.  The condition code and trap mnemonics are those used
       in the Machine Instruction Set manual.
*/



#include "hp3000_defs.h"
#include "hp3000_cpu.h"
#include "hp3000_cpu_fp.h"
#include "hp3000_mem.h"



/* Program constants */

#define MAX_DIGITS          28                  /* maximum number of decimal digits accepted */
#define MAX_WORDS           6                   /* maximum number of words needed for conversion */

#define MAX_COUNT_MASK      000037u             /* maximum shift count mask */

#define NOT_SET             MAX_DIGITS          /* indicator that an index is not set */


/* Packed decimal constants */

#define SIGN_PLUS           0014u               /* 1100 -> the number is positive */
#define SIGN_MINUS          0015u               /* 1101 -> the number is negative */
#define SIGN_UNSIGNED       0017u               /* 1111 -> the number is unsigned */


/* External decimal constants */

typedef enum {                                  /* shift mode, corresponds to EIS subopcode */
    Left        = 006,                          /*   SLD  (020606) */
    Normalizing = 007,                          /*   NSLD (020607) */
    Right       = 010                           /*   SRD  (020610) */
    } SHIFT_MODE;

typedef enum {                                  /* numeric sign values */
    Negative,
    Unsigned,
    Positive
    } NUMERIC_SIGN;

static HP_BYTE sign_digit [3] =                 /* sign digit, indexed by NUMERIC_SIGN */
    { SIGN_MINUS,                               /*   Negative */
      SIGN_UNSIGNED,                            /*   Unsigned */
      SIGN_PLUS };                              /*   Positive */

static HP_BYTE overpunch [3] [10] = {                       /* sign overpunches, indexed by NUMERIC_SIGN and value */
    { '}', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R' },   /*   Negative */
    { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' },   /*   Unsigned */
    { '{', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I' }    /*   Positive */
    };


/* Digit accessors.

   Decimal numbers are stored in memory as byte-addressable arrays.  Two number
   formats are supported.  Packed decimal numbers contain binary-coded-decimal
   (BCD) digits stored two per byte.  External decimal numbers contain ASCII
   digits with an optional overpunched sign in the last digit position; they are
   stored one per byte.

   Digit accessors extend the byte accessor structure to contain additional
   information useful in manipulating decimal numbers.  A digit accessor is
   initialized in the same manner as a byte accessor, with an additional
   parameter to specify the desired numeric format.  Routines are provided to
   read and write decimal numbers via accessors.  Unlike byte accessors, digit
   accessors contain a buffer large enough to hold the maximum number of digits
   allowed in a decimal number.  Every decimal number is right-justified in the
   buffer with leading zeros as necessary.  The accessor maintains a count of
   the actual number of digits specified, so that reading and writing of shorter
   numbers is handled transparently.
*/

typedef enum {                                  /* decimal number format */
    Packed,                                     /*   packed decimal */
    External                                    /*   external decimal */
    } DECIMAL_FORMAT;

typedef struct {                                /* decimal number accessor */
    BYTE_ACCESS     bac;                        /*   the underlying byte accessor */
    DECIMAL_FORMAT  format;                     /*   the format of the decimal number */
    HP_WORD         byte_offset;                /*   the byte offset for the byte accessor routines */
    uint32          starting_index;             /*   the index of the first digit in the number */
    uint32          significant_index;          /*   the index of the first significant digit in the number */
    uint32          digit_count;                /*   the count of digits in the number */
    NUMERIC_SIGN    sign;                       /*   the sign of the number */
    HP_BYTE         digits [MAX_DIGITS];        /*   the digits of the number */
    } DIGIT_ACCESS;


/* EIS local utility routine declarations */

static void   init_decimal  (DIGIT_ACCESS *dap, DECIMAL_FORMAT format, ACCESS_CLASS class,
                               HP_WORD byte_offset, HP_WORD digit_count);
static uint32 read_decimal  (DIGIT_ACCESS *dap);
static void   write_decimal (DIGIT_ACCESS *dap, t_bool merge_digits);

static HP_WORD compare_decimal  (DIGIT_ACCESS *first,        DIGIT_ACCESS *second);
static uint32  add_decimal      (DIGIT_ACCESS *augend,       DIGIT_ACCESS *addend);
static uint32  subtract_decimal (DIGIT_ACCESS *minuend,      DIGIT_ACCESS *subtrahend);
static uint32  multiply_decimal (DIGIT_ACCESS *multiplicand, DIGIT_ACCESS *multiplier);
static uint32  shift_decimal    (DIGIT_ACCESS *target,       DIGIT_ACCESS *source, SHIFT_MODE shift);
static uint32  convert_decimal  (DIGIT_ACCESS *target,       DIGIT_ACCESS *source);

static uint32  convert_binary   (DIGIT_ACCESS *decimal, HP_WORD address, HP_WORD count);

static t_bool read_operands   (DIGIT_ACCESS   *first, DIGIT_ACCESS *second, uint32 *trap);
static void   write_operand   (DIGIT_ACCESS   *operand);
static void   set_cca_decimal (DIGIT_ACCESS   *dap);
static void   decrement_stack (uint32 trap,   uint32 count_0, uint32 count_1, uint32 count_2);
static uint32 strip_overpunch (HP_BYTE *byte, NUMERIC_SIGN *sign);

static void fprint_decimal_operand (DIGIT_ACCESS *op, char *label);



/* EIS global routines */


/* Execute an EIS floating point operation.

   This routine is called to execute the floating point instruction currently in
   the CIR.  The instruction format is:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   0 | 1 | EIS FP op |  EIS FP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     EIS FP Opcode:

       0 = EADD (Extended precision add)
       1 = ESUB (Extended precision subtract)
       2 = EMPY (Extended precision multiply)
       3 = EDIV (Extended precision divide)
       4 = ENEG (Extended precision negate)
       5 = ECMP (Extended precision compare)
       6 = (undefined)
       7 = (undefined)

   Entry is with four TOS registers preloaded (this is done for all firmware
   extension instructions).  Therefore, no SR preload is needed here.
   Instructions that provide option bits to leave addresses on the stack do not
   modify those addresses during instruction execution.


   Implementation notes:

    1. Each instruction lists a potential stack underflow trap.  The underflow
       is actually detected in the firmware dispatcher, which has a stack
       preadjust of 4, before this routine is called.  The "cpu_pop" calls
       below always succeed, as the stack registers are fully populated on
       entry.

    2. The MICRO_ABORT macro does a longjmp to the microcode abort handler in
       the sim_instr routine.  If the user trap bit (T-bit) in the status word
       is set, the routine clears the overflow bit (O-bit) and invokes the trap
       handler.  If the T-bit is clear, the routine sets the O-bit and continues
       with the next instruction.  Therefore, we do not need to check the T-bit
       here and can simply do an unconditional MICRO_ABORT if a trap is
       indicated.

    3. The instruction executors follow the microcode in the placement of bounds
       checks.

    4. The ECMP instruction checks operand addresses against SM rather than SM +
       SR.  Because SR = 4 on entry, this effectively checks the entire
       four-word operand (if the first word is below SM, then the fourth word is
       below RB) before retrieving the individual operand words as needed for
       the comparison.  Therefore, no queue-downs are needed.  Note that a
       bounds violation can occur even if the first words differ and no
       additional words are read.  The diagnostic tests for this.
*/

t_stat cpu_eis_fp_op (void)
{
t_bool  negative;
HP_WORD operand_x, operand_y, address_x, address_y;
FP_OPND operand_u, operand_v, operand_w;
uint32  opcode, index;
t_stat  status = SCPE_OK;

opcode = FMEXSUBOP (CIR);                               /* get the opcode from the instruction */

switch (opcode) {                                       /* dispatch the opcode */

    case 010:                                           /* EADD (CCA, O; STUN, STOV, ARITH) */
    case 011:                                           /* ESUB (CCA, O; STUN, STOV, ARITH) */
    case 012:                                           /* EMPY (CCA, O; STUN, STOV, ARITH) */
    case 013:                                           /* EDIV (CCA, O; STUN, STOV, ARITH) */
        while (SR > 3)                                  /* if more than three TOS register are valid */
            cpu_queue_down ();                          /*   then queue them down until exactly three are left */

        operand_u.precision = fp_e;                     /* set the operand precision to double float */
        operand_v.precision = fp_e;                     /*   and the result precision to double float */

        for (index = 0; index < 4; index++) {           /* read both operands */
            cpu_read_memory (data_checked, DB + RB + index & LA_MASK, &operand_u.words [index]);
            cpu_read_memory (data_checked, DB + RA + index & LA_MASK, &operand_v.words [index]);
            }

        STA &= ~STATUS_O;                               /* clear the overflow flag */

        operand_w =                                     /* call the floating-point executor */
           fp_exec ((FP_OPR) (opcode - 010 + fp_add),   /*   and convert the opcode */
                    operand_u, operand_v);              /*     to an arithmetic operation */

        for (index = 0; index < 4; index++)             /* write the result */
            cpu_write_memory (data_checked, DB + RC + index & LA_MASK, operand_w.words [index]);

        cpu_pop ();                                     /* delete two words */
        cpu_pop ();                                     /*   from the stack */

        SET_CCA (operand_w.words [0],                   /* set the condition code */
                 operand_w.words [1] | operand_w.words [2] | operand_w.words [3]);

        if (operand_w.trap != trap_None) {                  /* if an error occurred */
            if (operand_w.trap == trap_Ext_Float_Overflow   /*   then if the result overflowed */
              && (STA & STATUS_CC_MASK) == STATUS_CCE)      /*     to a zero value */
                SET_CCG;                                    /*       then set CCG */

            if ((STA & STATUS_T) == 0)                  /* if user traps are disabled */
                cpu_pop ();                             /*   then delete the result address */

            MICRO_ABORT (operand_w.trap);               /* trap or set overflow */
            }

        else                                            /* otherwise the operation completed normally */
            cpu_pop ();                                 /*   so delete the result address */
        break;


    case 014:                                               /* ENEG (CCA; STUN) */
        cpu_read_memory (data_checked, DB + RA & LA_MASK,   /* read the first word */
                         &operand_x);                       /*   of the operand */

        if (operand_x == 0) {                               /* if the first word is zero */
            for (index = 1; index < 4; index++) {           /*   then check the other words */
                cpu_read_memory (data_checked,              /*     to see if they are */
                                 DB + RA + index & LA_MASK, /*       all zero as well */
                                 &operand_y);

                if (operand_y != 0)                         /* if a non-zero word is seen */
                    break;                                  /*   then quit the check */
                }

            if (index == 4) {                           /* if the operand value is zero */
                SET_CCE;                                /*   then set CCE */
                cpu_pop ();                             /*     and delete the operand address */
                break;                                  /*       and return without rewriting the value */
                }
            }

        operand_x = operand_x ^ D16_SIGN;               /* complement the sign bit of the non-zero operand */

        cpu_write_memory (data_checked, DB + RA & LA_MASK,  /* write the updated value back */
                          operand_x);

        SET_CCA (operand_x, 1);                         /* set CCL or CCG from the sign bit */
        cpu_pop ();                                     /*   and delete the operand address from the stack */
        break;


    case 015:                                           /* ECMP (CCC; STUN) */
        address_x = DB + RB & LA_MASK;                  /* form the data offset */
        address_y = DB + RA & LA_MASK;                  /*   for the two operands */

        if (NPRV && (address_y < DL || address_y > SM)) /* if non-privileged and the operand is out of bounds */
            MICRO_ABORT (trap_Bounds_Violation);        /*   then trap for a bounds violation */

        cpu_pop ();                                     /* delete two words */
        cpu_pop ();                                     /*   from the stack */

        if (NPRV && (address_x < DL || address_x > SM)) /* if non-privileged and the operand is out of bounds */
            MICRO_ABORT (trap_Bounds_Violation);        /*   then trap for a bounds violation */

        cpu_read_memory (data, address_x, &operand_x);  /* read the first word */
        cpu_read_memory (data, address_y, &operand_y);  /*   of each of the two operands */

        negative = operand_x & D16_SIGN;                /* TRUE if first operand is negative */

        if ((operand_x ^ operand_y) & D16_SIGN)         /* if the operand signs differ */
            SET_CCA (operand_x, 1);                     /*   then set the condition on the first words excluding CCE */

        else if (operand_x != operand_y)                /* otherwise if the first operand words differ */
            if (negative)                               /*   then if they're both negative */
                SET_CCC (operand_y, 0, operand_x, 0);   /*     then reverse the comparison */
            else                                        /*   otherwise */
                SET_CCC (operand_x, 0, operand_y, 0);   /*     compare the integer operands */

        else {
            for (index = 1; index < 4; index++) {       /* otherwise compare the remaining words */
                cpu_read_memory (data, address_x + index & LA_MASK, &operand_x);
                cpu_read_memory (data, address_y + index & LA_MASK, &operand_y);

                if (operand_x != operand_y)             /* once the words differ */
                    break;                              /*   then the comparison is finished */
                }

            if (negative)                               /* if the operands are negative */
                SET_CCC (0, operand_y, 0, operand_x);   /*   then reverse the logical comparison */
            else                                        /* otherwise */
                SET_CCC (0, operand_x, 0, operand_y);   /*   compare the operand words logically */
            }
        break;


    default:
        status = STOP_UNIMPL;                           /* the firmware extension instruction is unimplemented */
    }

return status;                                          /* return the execution status */
}


/* Execute an EIS decimal arithmetic operation.

   This routine is called to execute the decimal arithmetic instruction
   currently in the CIR.  The instruction format is:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1 |  options  |  decimal op   |  EIS Decimal
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     Decimal Opcode:

       00 - (undefined)
       01 - DMPY (Double logical multiply)
       02 - CVAD (Convert ASCII to decimal)
       03 - CVDA (Convert decimal to ASCII)
       04 - CVBD (Convert binary to decimal)
       05 - CVDB (Convert decimal to binary)
       06 - SLD  (Shift left decimal)
       07 - NSLD (Normalizing shift left decimal)
       10 - SRD  (Shift right decimal)
       11 - ADDD (Add decimal)
       12 - CMPD (Compare decimal)
       13 - SUBD (Subtract decimal)
       14 - MPYD (Multiply decimal)
       15 - (undefined)
       16 - (undefined)
       17 - (undefined)

   Entry is with four TOS registers preloaded (this is done for all firmware
   extension instructions).  Therefore, no SR preload is needed here.
   Instructions that provide option bits to leave addresses on the stack do not
   modify those addresses during instruction execution.


   Implementation notes:

    1. Each instruction lists a potential stack underflow trap.  The underflow
       is actually detected in the firmware dispatcher, which has a stack
       preadjust of 4, before this routine is called.  The "cpu_pop" calls
       below always succeed, as the stack registers are fully populated on
       entry.

    2. All of the decimal instruction except DMPY, SLD, NSLD, and SRD, test for
       seven words of available stack space on entry (including the four words
       already present in the TOS registers) and trap for a stack overflow if
       they are not present.  In microode, the test passes if SM + 7 <= Z.  In
       simulation, the test compares SM plus SR plus a per-opcode addition to Z,
       which is equivalent.  The test is skipped for the four instructions
       above, although for SLD, NSLD, and SRD, the test is merely postponed
       until after the X register is masked to the lower five bits in the opcode
       handlers.

    3. The CVDA, SLD, NSLD, and SRD instructions test for trap conditions before
       setting the condition code for the operand.  As "read_decimal" sets the
       condition code, these instructions must save the status register on entry
       and restore if a trap is taken.

    4. If a bad decimal digit is present, the CVDA microcode converts and writes
       ASCII characters until the digit is encountered, resulting in a partial
       conversion before the trap is taken.  The diagnostic tests for this.

    5. For the CMPD instruction with both operands negative, XORing the
       condition code with STATUS_CCL flips the result of the magnitude
       comparison, i.e., CCL becomes CCG, and vice versa.
*/

t_stat cpu_eis_dec_op (void)
{
static const HP_WORD stack_check [] = {         /* extra stack words needed, indexed by opcode */
    0, 0, 3, 3, 3, 3, 0, 0,
    0, 3, 3, 3, 3, 0, 0, 0
    };

DIGIT_ACCESS source, target, left, right;
HP_WORD      entry_status, comparison;
uint32       opcode;
t_uint64     product;
uint32       trap   = trap_None;
t_stat       status = SCPE_OK;

opcode = FMEXSUBOP (CIR);                               /* get the opcode from the instruction */

if (stack_check [opcode] > 0                            /* if extra words on the stack are needed */
 && SM + SR + stack_check [opcode] > Z)                 /*   and they aren't available */
    MICRO_ABORT (trap_Stack_Overflow);                  /*     then trap for a stack overflow */

else switch (opcode) {                                  /* otherwise dispatch the opcode */

    case 001:                                           /* DMPY (CCA, C; STUN) */
        product = (t_uint64) TO_DWORD (RB, RA)          /* multiply the TOS double word */
                * (t_uint64) TO_DWORD (RD, RC);         /*   by the NOS double word */

        RD = HIGH_UPPER_WORD (product);                 /* separate */
        RC = LOW_UPPER_WORD (product);                  /*   then resulting */
        RB = UPPER_WORD (product);                      /*     quad word product */
        RA = LOWER_WORD (product);                      /*       and return in the TOS registers */

        SET_CARRY (RD | RC);                            /* set carry if the upper double-word is significant */
        SET_CCA (RD, RC | RB | RA);                     /*   and set the condition code for the product */
        break;


    case 002:                                           /* CVAD (CCA, O; STUN, STOV, ARITH) */
        if (RA > MAX_DIGITS || RC > MAX_DIGITS)         /* if the source or target digit counts are too large */
            trap = trap_Invalid_Decimal_Length;         /*   then trap for a count overflow */

        else if (RA > 0 && RC > 0) {                                /* otherwise if there are digits to process */
            init_decimal (&source, External, data_checked, RB, RA); /*   so set up digit accessors */
            init_decimal (&target, Packed, data_checked, RD, RC);   /*     for the source and target decimals */

            read_decimal (&source);                     /* read the source ASCII number, ignoring errors */

            if (TRACING (cpu_dev, DEB_MOPND))
                fprint_decimal_operand (&source, "source");

            trap = convert_decimal (&target, &source);  /* convert ASCII to packed decimal and check for errors */

            write_decimal (&target, FALSE);             /* write the decimal with a leading zero if required */

            if (TRACING (cpu_dev, DEB_MOPND))
                fprint_decimal_operand (&target, "result");

            set_cca_decimal (&target);                  /* set CCA on the decimal result */
            }

        decrement_stack (trap, 2, 4, 0);                /* decrement the stack and trap if indicated */
        break;


    case 003:                                           /* CVDA (CCA, O; STUN, STOV, ARITH) */
        while (SR > 3)                                  /* if more than three TOS register are valid */
            cpu_queue_down ();                          /*   then queue them down until exactly three are left */

        if (RB > MAX_DIGITS)                            /* if the target digit count is too large */
            trap = trap_Invalid_Decimal_Length;         /*   then trap for a count overflow */

        else if (RB > 0) {                              /* otherwise if there are digits to process */
            entry_status = STA;                         /*   then save the entry status for potential rollback */

            init_decimal (&source, Packed, data_checked, RA, RB);   /* set up digit accessors */
            init_decimal (&target, External, data_checked, RC, RB); /*   for the source and target decimals */

            read_decimal (&source);                     /* read the source decimal number, ignoring errors */

            if (TRACING (cpu_dev, DEB_MOPND))
                fprint_decimal_operand (&source, "source");

            trap = convert_decimal (&target, &source);  /* convert packed decimal to ASCII and check for errors */

            write_decimal (&target, TRUE);              /* write the decimal number to memory */

            if (TRACING (cpu_dev, DEB_MOPND))
                fprint_decimal_operand (&target, "result");

            if (trap == trap_None)                      /* if the conversion succeeded */
                set_cca_decimal (&target);              /*   then set CCA on the decimal result */
            else                                        /* otherwise */
                STA = entry_status;                     /*   restore the original entry status */
            }

        decrement_stack (trap, 1, 3, 0);                /* decrement the stack and trap if indicated */
        break;


    case 004:                                           /* CVBD (CCA, O; STUN, STOV, ARITH) */
        if (RA > MAX_WORDS)                             /* if the source word count is too large */
            trap = trap_Invalid_Word_Count;             /*   then trap for a word count overflow */

        else if (RC > MAX_DIGITS)                       /* otherwise if the target digit count is too large */
            trap = trap_Invalid_Decimal_Length;         /*   then trap for a count overflow */

        else if (RA > 0 && RC > 0) {                                /* otherwise if there are words to process */
            init_decimal (&target, Packed, data_checked, RD, RC);   /*   then set up the target digit accessor */

            trap = convert_binary (&target, RB, RA);    /* convert the binary number (RB,RA) to decimal */

            write_decimal (&target, TRUE);              /* write the (possibly truncated) decimal number */

            if (TRACING (cpu_dev, DEB_MOPND))
                fprint_decimal_operand (&target, "result");

            set_cca_decimal (&target);                  /* set CCA on the decimal result */
            }

        decrement_stack (trap, 2, 4, 0);                /* decrement the stack and trap if indicated */
        break;


    case 005:                                           /* CVDB (CCA, O; STUN, STOV, ARITH) */
        while (SR > 3)                                  /* if more than three TOS register are valid */
            cpu_queue_down ();                          /*   then queue them down until exactly three are left */

        if (RA > MAX_DIGITS)                            /* if the source digit count is too large */
            trap = trap_Invalid_Decimal_Length;         /*   then trap for a count overflow */

        else if (RA > 0) {                                          /* otherwise if there are digits to process */
            init_decimal (&source, Packed, data_checked, RB, RA);   /*   then set up the source digit accessor */

            trap = convert_binary (&source, RC, RA);    /* convert the decimal number to binary (RC,RA) */

            if (trap == trap_None)                      /* if the source decimal was valid */
                set_cca_decimal (&source);              /*   then set CCA on the decimal result */
            }

        decrement_stack (trap, 2, 3, 0);                /* decrement the stack and trap if indicated */
        break;


    case 006:                                           /* SLD  (CCA, C, O; STUN, STOV, ARITH) */
    case 007:                                           /* NSLD (CCA, C, O; STUN, STOV, ARITH) */
        SET_CARRY (FALSE);                              /* clear carry in anticipation of a good result */

        /* fall through into SRD case */

    case 010:                                           /* SRD  (CCA, O; STUN, STOV, ARITH) */
        entry_status = STA;                             /* save the entry status for potential rollback */

        X = X & MAX_COUNT_MASK;                         /* mask the shift count to the lower five bits */

        if (SM + SR + 3 > Z)                            /* if there aren't three free words on the stack */
            MICRO_ABORT (trap_Stack_Overflow);          /*   then trap for a stack overflow */

        else if (RA > MAX_DIGITS || RC > MAX_DIGITS)    /* otherwise if the source or target counts are too large */
            trap = trap_Invalid_Decimal_Length;         /*   then trap for a count overflow */

        else if (RA > 0 && RC > 0) {                        /* otherwise if there are digits to process */
            init_decimal (&source, Packed, data_checked, RB, RA);   /*   so set up digit accessors */
            init_decimal (&target, Packed, data_checked, RD, RC);   /*     for the source and target decimals */

            trap = read_decimal (&source);              /* read the source decimal number */

            if (TRACING (cpu_dev, DEB_MOPND))
                fprint_decimal_operand (&source, "source");

            if (trap == trap_None) {                        /* if the source number is valid */
                trap = shift_decimal (&target, &source,     /*   then shift the number as indicated */
                                      (SHIFT_MODE) opcode); /*     by the instruction opcode */

                if (trap == trap_None) {                /* if the shift succeeded */
                    write_decimal (&target, TRUE);      /*   then write the result to memory */

                    if (TRACING (cpu_dev, DEB_MOPND))
                        fprint_decimal_operand (&target, "target");

                    set_cca_decimal (&target);          /* set CCA on the decimal result */
                    }

                else                                        /* otherwise the shift failed */
                    STA = STA & STATUS_C | entry_status;    /*   so restore the status but keep the carry bit */
                }
            }

        decrement_stack (trap, 0, 2, 4);                /* decrement the stack and trap if indicated */
        break;


    case 011:                                           /* ADDD (CCA, O; STUN, STOV, ARITH) */
        if (read_operands (&source, &target, &trap)) {  /* read the decimal operands; if they are valid */
            trap = add_decimal (&target, &source);      /*   then add them */
            write_operand (&target);                    /*     and write the result back */
            }

        decrement_stack (trap, 0, 2, 4);                /* decrement the stack and trap if indicated */
        break;


    case 012:                                               /* CMPD (CCC, O; STUN, STOV, ARITH) */
        if (read_operands (&right, &left, &trap)) {         /* read the decimal operands; if they are valid */
            comparison = compare_decimal (&left, &right);   /*   then compare the operand magnitudes */

            if (left.sign == Negative                   /* if the operand signs are the same */
              && right.sign == Negative                 /*   and negative */
              && comparison != STATUS_CCE)              /*     and the values aren't equal */
                comparison = comparison ^ STATUS_CCL;   /*       then flip the magnitude comparison */

            else if (right.sign != left.sign            /* otherwise if the signs are different */
              && (right.significant_index != NOT_SET    /*   and the comparison */
              || left.significant_index != NOT_SET))    /*     is not +0 = -0 */
                if (right.sign == Negative)             /*       then if the right operand is negative */
                    comparison = STATUS_CCG;            /*         then the left is greater (positive) */
                else                                    /*       otherwise the right is positive */
                    comparison = STATUS_CCL;            /*         so the left is smaller (negative) */

            STA = STA & ~STATUS_CC_MASK | comparison;   /* set the condition code */
            }

        decrement_stack (trap, 0, 2, 4);                /* decrement the stack and trap if indicated */
        break;


    case 013:                                           /* SUBD (CCA, O; STUN, STOV, ARITH) */
        if (read_operands (&source, &target, &trap)) {  /* read the decimal operands; if they are valid */
            trap = subtract_decimal (&target, &source); /*   then subtract them */
            write_operand (&target);                    /*     and write the result back */
            }

        decrement_stack (trap, 0, 2, 4);                /* decrement the stack and trap if indicated */
        break;


    case 014:                                           /* MPYD (CCA, O; STUN, STOV, ARITH) */
        if (read_operands (&source, &target, &trap)) {  /* read the decimal operands; if they are valid */
            trap = multiply_decimal (&target, &source); /*   then multiply them */
            write_operand (&target);                    /*     and write the result back */
            }

        decrement_stack (trap, 0, 2, 4);                /* decrement the stack and trap if indicated */
        break;


    default:
        status = STOP_UNIMPL;                           /* the firmware extension instruction is unimplemented */
    }

return status;                                          /* return the execution status */
}



/* EIS local utility routine declarations */


/* Initialize a decimal accessor.

   The supplied decimal accessor structure is initialized for the numeric
   format, starting relative byte offset pointer, digit count, and type of
   memory access desired.  If checked accesses are requested, then the starting
   and ending word addresses will be bounds-checked, and a Bounds Violation will
   occur if the address range exceeds that permitted by the access.

   On return, the decimal accessor is ready for use with the other decimal
   access routines.

   Decimal accessors may be used to sequentially read or write packed or
   external decimal numbers from or to memory.  Packed numbers store two BCD
   digits per byte, except for the last byte, which contains the LSD and the
   sign, and the first byte, which contains a single digit if the count of
   digits is even.  External numbers store one ASCII digit per byte.  The read
   and write routines handle the digit packing and unpacking automatically.


   Implementation notes:

    1. The underlying byte access routines assume that if the initial range or
       starting address is checked, succeeding accesses need not be checked, and
       vice versa.  The implication is that if the access class passed to this
       routine is checked, the routine might abort with a Bounds Violation, but
       succeeding read or write accesses will not, and if the class is
       unchecked, this routine will not abort but a succeeding access might.
*/

static void init_decimal (DIGIT_ACCESS *dap, DECIMAL_FORMAT format, ACCESS_CLASS class,
                          HP_WORD byte_offset, HP_WORD digit_count)
{
int    zero;
uint32 byte_count;

dap->format = format;                                   /* set the decimal number format */

if (format == Packed) {                                 /* if a packed number is designated */
    byte_count = digit_count / 2 + 1;                   /*   then convert the digit count to a byte count */
    dap->sign = Positive;                               /* set the sign of the zero value */
    zero = 0;                                           /*   and initialize with a numeric zero */
    }

else {                                                  /* otherwise an external number is designated */
    byte_count = digit_count;                           /*   so the byte count is the digit count */
    dap->sign = Unsigned;                               /* set the sign of the zero value */
    zero = (int) '0';                                   /*   and initialize with a character zero */
    }

dap->byte_offset = byte_offset;                         /* save the offset to the first byte to access */

mem_init_byte (&dap->bac, class, &dap->byte_offset, byte_count);    /* set up a byte accessor for the digits */

dap->significant_index = NOT_SET;                       /* initialize the significant digit index */
dap->starting_index    = MAX_DIGITS - digit_count;      /*   and save the index of the first valid digit */
dap->digit_count       = digit_count;                   /*     and the number of valid digits */

memset (dap->digits, zero, sizeof dap->digits);         /* store zeros in the full digit array */

return;
}


/* Read a decimal number from memory.

   The decimal number indicated by the supplied decimal accessor is read from
   memory into the accessor's digit array and checked for correctness.  The
   routine returns trap_Invalid_Decimal_Digit or trap_Invalid_ASCII_Digit if
   invalid digit is encountered, depending on the accessor format.  If all of
   the digits are legal representations, trap_None is returned after the index
   of the first significant digit in the number is determined.  The index is
   that of the first non-zero digit after the leading zeros.  If the decimal
   number is zero, the significant digit index will equal the maximum digit
   count, i.e., will point just beyond the last digit.

   Special handling is needed for the first byte of a packed decimal number  if
   the digit count is even.  In this case, only the right-hand digit within the
   byte is part of the number.  Otherwise, all bytes contain two digits except
   for the last, which contains a digit and the sign.


   Implementation notes:

    1. For packed decimal numbers only, the microcode sets the condition code to
       CCL or CCG, depending on the sign of the number, before checking digits
       for validity.  Consequently, if an invalid digit trap is taken, the
       condition code has already been set.  We follow that behavior here.
*/

static uint32 read_decimal (DIGIT_ACCESS *dap)
{
HP_BYTE byte, upper_digit, lower_digit;
uint32  byte_count, index;
uint32  digit_trap = trap_None, sign_trap = trap_None;

index = dap->starting_index;                            /* get the index of the first digit to store */
byte_count = dap->bac.length;                           /*   and the number of bytes to read */

dap->significant_index = NOT_SET;                       /* initialize the index of the first significant digit */

if (dap->format == Packed) {                            /* if this is a packed decimal value */
    if ((dap->digit_count & 1) == 0) {                  /*   then if the digit count is even */
        byte = mem_read_byte (&dap->bac);               /*     then read the byte containing the single MSD */
        byte_count--;                                   /*       and drop the remaining count */

        lower_digit = LOWER_HALF (byte);                /* get the right digit from the byte */

        if (lower_digit > 9)                            /* if the digit is invalid */
            digit_trap = trap_Invalid_Decimal_Digit;    /*   then set up the trap */
        else if (lower_digit > 0)                       /* otherwise if the digit is non-zero */
            dap->significant_index = index;             /*   then it is the first significant digit */

        dap->digits [index++] = lower_digit;            /* save it as the first digit */
        }

    while (byte_count > 0) {                            /* for the remaining bytes */
        byte = mem_read_byte (&dap->bac);               /*   read the next byte from memory */
        byte_count--;                                   /*     and drop the remaining count */

        upper_digit = UPPER_HALF (byte);                /* split the byte */
        lower_digit = LOWER_HALF (byte);                /*   into left and right digits */

        if (upper_digit > 9)                                        /* if the digit is invalid */
            digit_trap = trap_Invalid_Decimal_Digit;                /*   then set up the trap */
        if (upper_digit > 0 && dap->significant_index == NOT_SET)   /* otherwise if it's non-zero and not yet indexed */
            dap->significant_index = index;                         /*   then save the first significant digit index */

        dap->digits [index++] = upper_digit;            /* save the left-hand digit */

        if (byte_count == 0)                            /* if this is the last byte */
            if (lower_digit == SIGN_MINUS) {            /*   then if a minus sign is present */
                SET_CCL;                                /*     then preset the condition code to "less than" */
                dap->sign = Negative;                   /*       and set the decimal sign negative */
                }

            else {                                      /* otherwise */
                SET_CCG;                                /*   preset the condition code to "greater than" */

                if (lower_digit == SIGN_UNSIGNED)       /* if an unsigned indicator is present */
                    dap->sign = Unsigned;               /*   then the decimal is unsigned */
                else                                    /* otherwise a plus sign is assumed */
                    dap->sign = Positive;               /*   and the decimal is positive */
                }

        else {                                              /* otherwise this is an intermediate byte */
            if (lower_digit > 9)                            /*   so if the digit is invalid */
                digit_trap = trap_Invalid_Decimal_Digit;    /*     then set up the trap */

            else if (lower_digit > 0                    /* otherwise if it's non-zero */
              && dap->significant_index == NOT_SET)     /*   and not yet indexed */
                dap->significant_index = index;         /*     then save the first significant digit index */

            dap->digits [index++] = lower_digit;        /* save the right-hand byte */
            }
        }
    }

else if (dap->format == External)                       /* otherwise it's an external decimal */
    while (byte_count > 0) {                            /* for the remaining bytes */
        byte = mem_read_byte (&dap->bac);               /*   read the byte from memory */
        byte_count--;                                   /*     and drop the remaining count */

        if (byte_count == 0 && byte != ' ')                     /* if this is a non-blank overpunched sign */
            sign_trap = strip_overpunch (&byte, &dap->sign);    /*   then strip the overpunch and set the sign */

        else if (byte < '0' && byte != ' ' || byte > '9')   /* otherwise if the digit is not valid */
            digit_trap = trap_Invalid_ASCII_Digit;          /*   then trap for the error */

        if (byte > '0' && dap->significant_index == NOT_SET)    /* if it's non-zero and not yet indexed */
            dap->significant_index = index;                     /*   the save the first significant digit index */

        dap->digits [index++] = byte;                   /* save the byte */
        }

if (digit_trap != trap_None)                            /* if a bad digit was seen */
    return digit_trap;                                  /*   then return the trap code */
else                                                    /* otherwise */
    return sign_trap;                                   /*   return success or a bad sign trap code */
}


/* Write a decimal number to memory.

   The decimal number indicated by the supplied decimal accessor is written to
   memory.  Special handling is needed for the first byte of a packed decimal
   number if the digit count is even.  In this case, only the right-hand digit
   within the byte is part of the number.  Whether the most-significant digit is
   merged with the left-hand four bits of the byte or whether those bits are
   zeroed is determined by the supplied "merge_digits" parameter.  The parameter
   is ignored when writing numbers in external decimal format.


   Implementation notes:

    1. The CVAD instruction is the only one that does NOT merge the MSD into the
       leading byte.  It is also the only instruction that can write a negative
       zero number.

    2. Error aborts (e.g., from the CVDA instruction) may request writing fewer
       digits than are indicated by the starting index by reducing the byte
       accessor length.  Therefore, we use the latter instead of the former to
       determine the number of bytes to write.
*/

static void write_decimal (DIGIT_ACCESS *dap, t_bool merge_digits)
{
HP_BYTE byte, upper_digit, lower_digit;
uint32  byte_count, index;

index = dap->starting_index;                            /* get the index of the first digit to store */
byte_count = dap->bac.length;                           /*   and the number of bytes to read */

if (byte_count == 0)                                    /* if there are no bytes to write */
    return;                                             /*   then quit now */

if (dap->format == Packed) {                                /* if this is a packed decimal value */
    if (dap->significant_index == NOT_SET && merge_digits)  /*   then if the value is zero and merged */
        dap->sign = Positive;                               /*     then ensure that we write a positive zero */

    if ((dap->digit_count & 1) == 0) {                  /* if the digit count is even */
        if (merge_digits) {                             /*   then if the MSD must be merged */
            byte = mem_read_byte (&dap->bac);           /*     then get the current byte value */

            mem_reset_byte (&dap->bac);                 /* reset the byte accessor back to its original location */

            byte = TO_BYTE (UPPER_HALF (byte),          /* merge the MSD with the existing value */
                            dap->digits [index++]);     /*   in the byte */
            }

        else                                            /* otherwise merging is not required */
            byte = dap->digits [index++];               /*   so set the upper half of the byte to zero */

        mem_write_byte (&dap->bac, byte);               /* write the initial byte to memory */
        byte_count--;                                   /*   and drop the remaining count */
        }

    while (byte_count > 0) {                            /* for the remaining bytes */
        upper_digit = dap->digits [index++];            /*   get the left-hand digit */

        if (byte_count > 1)                             /* if this is an intermediate byte */
            lower_digit = dap->digits [index++];        /*   then get the right-hand digit */
        else                                            /* otherwise it's the last byte */
            lower_digit = sign_digit [dap->sign];       /*   so get the sign instead */

        byte = TO_BYTE (upper_digit, lower_digit);      /* merge the digits in the byte */

        mem_write_byte (&dap->bac, byte);               /* write the byte to memory */
        byte_count--;                                   /*   and drop the remaining count */
        }
    }

else if (dap->format == External)                       /* otherwise it's an external decimal */
    do {                                                /*   so for each digit */
        byte = dap->digits [index++];                   /*     get the next digit */

        if (byte_count == 1                             /* if this is the last byte */
          && byte >= '0' && byte <= '9')                /*   and the digit is valid */
            byte = overpunch [dap->sign] [byte - '0'];  /*     then get the overpunched sign */

        mem_write_byte (&dap->bac, byte);               /* write the byte to memory */
        }
    while (--byte_count > 0);                           /* continue until all bytes are written */

mem_update_byte (&dap->bac);                            /* write any partial final word if present */

return;
}


/* Compare two decimal numbers.

   This routine compares the magnitudes of two decimal numbers and returns a
   condition code to indicate the result (cc = first < | = | > second).  The
   signs of the numbers are not considered.

   If one number has more significant digits than the other, then it is larger
   by definition, and vice versa.  If both numbers have the same number of
   significant digits, then the corresponding digits of each number are compared
   until they differ or until all digits have been examined.  If both numbers
   have no significant digits, then both are zero values and so are equal.

   The returned status code represents the comparison of the first operand to
   the second operand.
*/

static HP_WORD compare_decimal (DIGIT_ACCESS *first, DIGIT_ACCESS *second)
{
uint32 index;

if (first->significant_index < second->significant_index)       /* if the first has more significant digits */
    return STATUS_CCG;                                          /*   than the second, then it is greater in value */

else if (first->significant_index > second->significant_index)  /* otherwise if the first has fewer significant */
    return STATUS_CCL;                                          /*   digits, then it is smaller in value */

else {                                                  /* otherwise they have the same significance */
    index = first->significant_index;                   /*   so they must be examined digit by digit */

    while (index < MAX_DIGITS) {                                    /* while digits remain */
        if (first->digits [index] > second->digits [index])         /*   if the first digit is greater */
            return STATUS_CCG;                                      /*     then the first operand is greater */

        else if (first->digits [index] < second->digits [index])    /* otherwise if the digit is smaller */
            return STATUS_CCL;                                      /*   then the first operand is smaller */

        index++;                                        /* otherwise they are equal, so try the next pair */
        }

    return STATUS_CCE;                                  /* all digits are equal, so the operands are equal */
    }
}


/* Add two decimal numbers.

   The sum of the two decimal operands is returned in the accessor of the first
   operand (augend = augend + addend).  If one operand is zero and the other is
   not, then the non-zero operand is returned as the sum.  Otherwise, the
   operands are added digit-by-digit.

   To ensure that the sum does not underflow (i.e., that the magnitude of the
   sum is always greater than zero), the operands are compared.  If the operand
   signs are the then same, the result is the sum of the magnitudes.  If the
   signs are different, then the sum is the smaller value subtracted from the
   larger value, and the result adopts the sign of the larger value.  If the
   magnitudes are equal and the signs are opposite, then the result is zero.

   Addition or subtraction is performed digit-by-digit, with carries between
   digits, until the all of the digits of the larger operand have been
   processed.

   A Decimal Overflow trap is returned if the result does not fit in the augend
   operand.


   Implementation notes:

    1. If the augend is zero, the addend digits must be copied to the augend
       digit array.  However, the ADDD and SUBD instructions allow the two
       operands to overlap.  Therefore, the "memmove" function must be used to
       copy the digits instead of the "memcpy" function.

    2. A borrow out of the last digit cannot occur because the operands are
       reordered if necessary to ensure that the difference is always positive.
*/

static uint32 add_decimal (DIGIT_ACCESS *augend, DIGIT_ACCESS *addend)
{
DIGIT_ACCESS *op1, *op2;
NUMERIC_SIGN operator;
HP_WORD      comparison;
HP_BYTE      carry;
int32        result;
uint32       index, last;

if (addend->significant_index == NOT_SET)               /* if the addend is zero */
    return trap_None;                                   /*   then the augend value is the sum */

else if (augend->significant_index == NOT_SET) {        /* otherwise if the augend is zero */
    memmove (augend->digits, addend->digits,            /*   then copy the addend value */
             sizeof augend->digits);                    /*     into the augend digit array */

    augend->sign = addend->sign;                            /* copy the addend sign */
    augend->significant_index = addend->significant_index;  /*   and index of significant digits */

    if (addend->significant_index < augend->starting_index) /* if the augend does not have enough room */
        return trap_Decimal_Overflow;                       /*   then an overflow occurs */
    else                                                    /* otherwise */
        return trap_None;                                   /*   the addend value is the sum */
    }

else {                                                  /* otherwise neither value is zero */
    comparison = compare_decimal (augend, addend);      /*   so compare the operand magnitudes */

    if (comparison == STATUS_CCL) {                     /* if the augend is smaller than the addend */
        op1 = addend;                                   /*   then swap the order */
        op2 = augend;                                   /*     for the sum or difference */
        }

    else {                                              /* otherwise the augend is larger than the addend */
        op1 = augend;                                   /*   so keep the supplied order */
        op2 = addend;                                   /*     for the sum or difference */
        }

    if (augend->sign == addend->sign)                   /* if the operand signs are the same */
        operator = Positive;                            /*   then sum the magnitudes */

    else if (comparison == STATUS_CCE) {                /* otherwise if the values are equal with different signs */
        memset (augend->digits, 0,                      /*   then the sum is zero */
                sizeof augend->digits);

        augend->sign = Positive;                        /* the result is positive */
        augend->significant_index = NOT_SET;            /*   with no significant digits */
        return trap_None;                               /*     and no error */
        }

    else {                                              /* otherwise the sum is determined */
        operator = Negative;                            /*   by subtracting the magnitudes */
        augend->sign = op1->sign;                       /*     and assuming the sign of the larger operand */
        }
    }

last = op1->significant_index;                          /* stop after processing the MSD of the larger value */

augend->significant_index = NOT_SET;                    /* reset the result significant digit index */

carry = 0;                                              /* start with no carry */
index = MAX_DIGITS;                                     /*   and with the LSD and work forward */

do {                                                    /* sum the digits in sequence */
    index--;                                            /* move the index to the next digit */

    if (operator == Positive)                                       /* if we're summing */
        result = op1->digits [index] + op2->digits [index] + carry; /*   then add the digits and carry */
    else                                                            /* otherwise */
        result = op1->digits [index] - op2->digits [index] - carry; /*   subtract the digits and borrow */

    if (result > 9) {                                   /* if a carry occurred */
        result = LOWER_HALF (result + 6);               /*   then correct the digit */
        carry = 1;                                      /*     and set the carry */
        }

    else if (result < 0) {                              /* otherwise if a borrow occurred */
        result = result + 10;                           /*   then correct the digit */
        carry = 1;                                      /*     and set the borrow */
        }

    else                                                /* otherwise */
        carry = 0;                                      /*   neither carry nor borrow was generated */

    if (result > 0 && index >= augend->starting_index)  /* if a significant digit that will fit in the result */
        augend->significant_index = index;              /*   then count it */

    augend->digits [index] = (HP_BYTE) result;          /* save the digit */
    }
while (index > last);                                   /*   and continue until all significant digits processed */

if (carry > 0 && index > 0) {                           /* if a carry out of the last significant digit occurs */
    augend->digits [--index] = carry;                   /*   then store it in the next MSD */
    carry = 0;                                          /*     and indicate that space was available for it */

    if (index >= augend->starting_index)                /* if the carry did not overflow the available space */
        augend->significant_index = index;              /*   then it becomes the most significant digit */
    }

if (carry > 0 || augend->starting_index > index)        /* if there is insufficient room to contain the result */
    return trap_Decimal_Overflow;                       /*   then indicate an overflow */
else                                                    /* otherwise */
    return trap_None;                                   /*   the addition succeeded */
}


/* Subtract two decimal numbers.

   The difference of the two decimal operands is returned in the accessor of the
   first operand (minuend = minuend - subtrahend).  Subtraction is implemented
   by negating the subtrahend and then adding the minuend.


   Implementation notes:

    1. If the subtrahend is zero, negating produces a "negative zero."  However,
       the "add" routine handles this as positive zero, so we do not need to
       worry about this condition.
*/

static uint32 subtract_decimal (DIGIT_ACCESS *minuend, DIGIT_ACCESS *subtrahend)
{
if (subtrahend->sign == Negative)                       /* invert the sign */
    subtrahend->sign = Positive;                        /*   of the subtrahend */
else
    subtrahend->sign = Negative;

return add_decimal (minuend, subtrahend);               /* add to obtain the difference */
}


/* Multiply two decimal numbers.

   The product of the two decimal operands is returned in the accessor of the
   first operand (multiplicand = multiplicand * multiplier).  Conceptually, the
   implementation is a 28 x 28 = 56-digit multiply with the lower 28 digits
   retained.  If either operand is zero, zero is returned as the product.
   Otherwise, the product is obtained by long multiplication (digit-by-digit
   multiplication and summation of the partial products) with the shorter of the
   two operands selected as the multiplier to improve efficiency.

   If the result would overflow 28 digits (the maximum allowed), the
   multiplication is not attempted, and no result is returned.  If the result
   fits in 28 digits but not in the space available for the result, the
   truncated result is returned.  In both cases, a Decimal Overflow trap is
   returned.  Otherwise, the product is returned in the multiplicand accessor,
   and no trap occurs.

   The product is obtained by multiplying the significant digits of the
   multiplicand by each significant digit of the multiplier and summing the
   partial products.  The sign of the product is positive if the operand signs
   are the same and negative if they differ.


   Implementation notes:

    1. An initial check is made to see if the product would exceed the maximum
       number of significant digits permitted (28).  If the sum of the numbers
       of significant digits exceeds 29, the product will not fit.  If the sum
       is 28 or less, the product will fit.  If the sum is 29, the product may
       fit, depending on the values of the operands.  In the last case, a carry
       out of the 28th digit indicates that the product won't fit.
*/

static uint32 multiply_decimal (DIGIT_ACCESS *multiplicand, DIGIT_ACCESS *multiplier)
{
DIGIT_ACCESS *op1, *op2;
HP_WORD      comparison;
HP_BYTE      product [MAX_DIGITS];
uint32       index_1, index_2, index_p, start_1, start_2, digit, partial, carry;

if (multiplicand->significant_index == NOT_SET)         /* if the multiplicand is zero */
    return trap_None;                                   /*   then it already hold the product */

else if (multiplier->significant_index == NOT_SET) {    /* otherwise if the multiplier is zero */
    memset (multiplicand->digits, 0,                    /*   then set the multiplicand value to zero */
            sizeof multiplicand->digits);               /*     by clearing the digit array */

    multiplicand->sign = Positive;                      /* the result is positive */
    multiplicand->significant_index = NOT_SET;          /*   with no significant digits */
    return trap_None;                                   /*     and no error */
    }

else if (multiplicand->significant_index                /* otherwise if the product would overflow */
  + multiplier->significant_index < MAX_DIGITS - 1)     /*   the maximum number of digits allowed */
    return trap_Decimal_Overflow;                       /*     then report it without trying */

else {                                                          /* otherwise neither value is zero */
    comparison = compare_decimal (multiplicand, multiplier);    /*   so compare the operand magnitudes */

    if (comparison == STATUS_CCL) {                     /* if the multiplicand is smaller than the multiplier */
        op1 = multiplier;                               /*   then swap the order */
        op2 = multiplicand;                             /*     to reduce the number of operations */
        }

    else {                                              /* otherwise the multiplicand is larger than the multiplier */
        op1 = multiplicand;                             /*   so keep the supplied order */
        op2 = multiplier;                               /*     which is already optimal */
        }
    }

if (multiplicand->sign == multiplier->sign)             /* if the operand signs are the same */
    multiplicand->sign = Positive;                      /*   then the result will be positive */
else                                                    /* otherwise */
    multiplicand->sign = Negative;                      /*   a negative value will result */

memset (product, 0, sizeof product);                    /* clear the product */

start_1 = op1->significant_index;                       /* get the indices of */
start_2 = op2->significant_index;                       /*   the first non-zero digits in each operand */

index_2 = MAX_DIGITS;                                   /* begin with the multiplier LSD and work toward the MSD */

do {                                                    /* form the partial products in sequence */
    index_p = index_2;                                  /* align the product sum with the multiplier digit */
    carry = 0;                                          /*   and start with no initial carry */

    digit = op2->digits [--index_2];                    /* get the next multiplier digit */

    if (digit > 0) {                                    /* if the partial product will contribute to the sum */
        index_1 = MAX_DIGITS;                           /*   then start at the multiplicand LSD and work forward */

        do {                                                /* form the next partial product */
            partial = product [--index_p] + carry           /*   from the sum of the current product and carry */
                        + op1->digits [--index_1] * digit;  /*     and the product of the next two operand digits */

            product [index_p] = partial % 10;           /* save the new current product digit */
            carry             = partial / 10;           /*   and carry any overflow to the next digit */
            }
        while ((index_1 > start_1 || carry > 0)         /* continue until the multiplicand is exhausted */
          && index_p > 0);                              /*   or the product has no more room */
        }
    }
while (index_2 > start_2);                              /* continue until the multiplier is exhausted */

if (carry > 0) {                                        /* if a carry out of the last digit occurred */
    multiplicand->bac.length = 0;                       /*   then skip writing back the result */
    return trap_Decimal_Overflow;                       /*     because it is larger than the maximum allowed */
    }

else {                                                  /* otherwise */
    multiplicand->significant_index = index_p;          /*   update the count of significant product digits */

    memcpy (multiplicand->digits, product,              /* copy the product digits */
            sizeof multiplicand->digits);               /*   back into the result accessor */

    if (multiplicand->significant_index                 /* if some significant digits will be lost */
      < multiplicand->starting_index)                   /*   because the result isn't large enough */
        return trap_Decimal_Overflow;                   /*     then signal an overflow */
    else                                                /* otherwise */
        return trap_None;                               /*   then correct product is returned */
    }
}


/* Shift a decimal number.

   The decimal number specified by the "source" accessor is shifted by the number
   of digits specified by the value in the X register in the direction and mode
   specified by the "shift" parameter and is returned in the "target" accessor.
   Three shift modes are supported:

     Shift Mode   Action
     -----------  ---------------------------------------------------------
     Right        Shift digits to the right, zero fill on the left
     Left         Shift digits to the left, zero fill on the right
     Normalizing  Same as Left, except stop shifting if a significant digit
                  would be lost

   For the Right shift mode, digits shifted off of the right end are lost.  For
   the Left shift mode, digits shifted off the left end are lost; if a lost
   digit is significant (i.e., non-zero), the Carry flag is set in the Status
   register.  The Normalizing shift mode is identical to the Left mode, except
   that shifting is stopped if a significant digit would be lost; the remaining
   shift count is present in the X register on return.  If no shifting can be
   done without losing a significant digit (i.e., the number of significant
   source digits is greater than the number of available target digits), Carry
   is set, and a Decimal Overflow trap is returned.  If the shift succeeds, no
   trap is returned.

   Entry is with the target value initialized to zero.  If the source value is
   zero, then shifting will not change the result, so the routine returns
   immediately.  Otherwise, shifting is performed by copying a selected set of
   digits from the source to the target.  This is accomplished by determining
   the starting and ending indices in the source digit array and the starting
   index in the target array.

   First, the proposed shift is examined to determine if significant digits will
   be lost due to an insufficiently large target.  This would occur if a left
   shift moved the most-significant digit outside of the available target space
   or a right shift failed to move the most-significant digit into the target
   space.  In the case of a normal left or right shift, the source index is
   advanced to the first digit to be transferred, and the target index is set to
   the first target digit.  For a normalizing left shift, the shift count is
   reduced to ensure that target will start with the most-significant source
   digit.  The left shifts also set Carry status; the right shift simply
   truncates the result.

   If no significant digits would be lost, the source and target indices are set
   as directed by the shift count and direction, and the index where the copy
   should end is calculated.  If the shift count is large enough to shift all
   source digits off either end of the target, no digits are copied, and the
   zero result is returned.

   The routine converts an unsigned source into a positive target but otherwise
   sets the target sign to that of the source.  As the requisite digits are
   copied, the significance of the target is computed, as losing significant
   digits from the source will render a smaller significance in the target.
*/

static uint32 shift_decimal (DIGIT_ACCESS *target, DIGIT_ACCESS *source, SHIFT_MODE shift)
{
uint32 source_index, target_index, end_index;

if (source->significant_index == NOT_SET)               /* if the source value is zero */
    return trap_None;                                   /*   then the target value is also zero */

else if (shift == Right) {
    if (source->significant_index + X < target->starting_index) {   /* if significant digits will be lost */
        source_index = target->starting_index - X;                  /*    then start at the first non-truncated digit */
        target_index = target->starting_index;                      /*      and copy to the first target digit */
        }

    else {                                              /* otherwise the leading digits will fit */
        source_index = source->significant_index;       /*   so start at the first significant digit */
        target_index = source_index + X;                /*     and target the desired shift location */
        }

    if (target_index < MAX_DIGITS)                              /* if there are target digits to move */
        end_index = source_index + MAX_DIGITS - target_index;   /*   then set up the ending source index */
    else                                                        /* otherwise the shift loses all digits */
        source_index = MAX_DIGITS;                              /*   so point beyond the source array */
    }

else if (shift == Left) {
    if (source->significant_index < target->starting_index + X) {   /* if significant digits will be lost */
        source_index = target->starting_index + X;                  /*    then start at the first non-truncated digit */
        target_index = target->starting_index;                      /*      and copy to the first target digit */

        SET_CARRY (TRUE);                                           /* set Carry to indicate a significance loss */
        }

    else {                                              /* otherwise all digits will fit */
        source_index = source->significant_index;       /*   so start at the first significant digit */
        target_index = source_index - X;                /*     and target the desired shift location */
        }

    end_index = MAX_DIGITS;                             /* set up the ending source index */
    }

else {
    if (source->significant_index < target->starting_index) {   /* if shift cannot be done without losing significance */
        SET_CARRY (TRUE);                                       /*   then set Carry status */
        return trap_Decimal_Overflow;                           /*     and trap for an overflow */
        }

    else                                                /* otherwise the leading digit will fit */
        source_index = source->significant_index;       /*   so start with the first significant digit */

    if (X > source_index - target->starting_index) {    /* if significant digits will be lost */
        target_index = target->starting_index;          /*   then start the copy at the first target digit */
        X = X - (source_index - target_index);          /*     and drop the shift count by the amount shifted */

        SET_CARRY (TRUE);                               /* set Carry to indicate a significance loss */
        }

    else                                                /* otherwise all source digits will fit */
        target_index = source_index - X;                /*   so target the desired shift location */

    end_index = MAX_DIGITS;                             /* set up the ending source index */
    }

if (source_index >= MAX_DIGITS)                         /* if all digits will be shifted out of the target */
    return trap_None;                                   /*   then the result is zero */
else if (source->sign == Unsigned)                      /* otherwise if the source is unsigned */
    target->sign = Positive;                            /*   then set the result positive */
else                                                    /* otherwise */
    target->sign = source->sign;                        /*   the result is the same sign as the source */

while (source_index < end_index) {                      /* while there are digits to move */
    if (target->significant_index == NOT_SET            /* if the significant digit count has not been set */
      && source->digits [source_index] > 0)             /*   and the next source digit is non-zero */
        target->significant_index = target_index;       /*     then mark it as significant */

    target->digits [target_index++] = source->digits [source_index++];  /* copy the digit to the result */
    }

return trap_None;                                       /* return success */
}


/* Convert between packed and external decimal.

   The supplied source operand is converted to the format of the target operand.
   If the target is in external decimal format, each packed decimal digit in the
   source is converted to ASCII and stored in the corresponding location in the
   target digit array.  Bits 9 and 10 of the instruction indicate how the sign
   is to be handled.  If bit 9 (NABS_FLAG) is set, the target is unsigned.  If
   bit 10 (ABS_FLAG) is set, the target is negative if the source is negative,
   otherwise it is unsigned.  If neither bit is set, the target adopts the
   source sign.

   If the target is packed, external decimal source digits are converted to BCD
   and stored in the target digit array.  Leading source blanks are allowed and
   are converted to zeros, but embedded blanks will cause a trap.  The source
   sign is decoded from the potentially overpunched LSD and set as the target
   sign.  Conversion stops if the target is filled; remaining source digits are
   not checked for validity.

   Invalid source digits will cause an Invalid Decimal Digit or Invalid ASCII
   Digit trap, depending on the format.  In addition, the partially converted
   value is present in the target to the same extent as in the microcode.  A
   decimal to ASCII conversion proceeds from left to right and stops at the
   point an invalid digit is encountered.  Any good digits converted to that
   point will be written to memory.

   For an ASCII to decimal conversion, the check is executed from right to left,
   and an Invalid ASCII Digit trap occurs if the check fails.  If a trap occurs,
   a partial decimal conversion may be done.  The rules are a bit arcane, and
   the diagnostic tests for expected results for bad source strings.

   The sign is checked first, so if it is bad, nothing is written.  Thereafter,
   words are written as they are converted until an invalid character is seen,
   whereupon an immediate trap is taken, and the word in which the bad character
   would appear is not written.  Because the decimal target may end in either
   the upper or lower byte of the last word, an illegal character as the
   next-to-last digit may or may not write a target word.  If the target ends in
   the upper byte, that word is written; if it ends in the lower byte, that word
   is not written.

   The situation is complicated by embedded blanks.  Because processing occurs
   from right to left, encountering one or more contiguous blanks may or may not
   be illegal, depending on whether any additional digits appear to the left of
   the blank(s).  Working backward, the first space encountered is replaced by a
   zero, and a flag is set to require all additional characters to be spaces
   (i.e., it assumes that it is processing leading spaces).  If a non-space
   character is subsequently encountered, the trap is taken at that point, and
   any prior blanks encountered are stored as valid zeros.

   This means, for example, that an embedded space that would go in the MSD of a
   word will write that word with a zero in the MSD and then trap on the NEXT
   valid digit.  So processing "1234 67" will write an "067F" word before
   trapping, but processing "12345 7" or "1234-67"  will write nothing.

   As another example, if the ASCII string consists of a "1" followed by eight
   blanks followed by the sign, and the target ends in the right-hand byte, a
   word of four zeros and a word of three zeros plus a sign would be written
   before the trap is taken.  However, if the string contained a "1" followed by
   two blanks followed by the sign, nothing would be written.

   The diagnostic checks these three cases:

     Step 463, source 7,"1234 67", result 3,"067F"
       stored as: 067F

     Step 464, source 9,"9=7654321", result 5,"54321F"
       stored as: 5432 1FFF

     Step 465, source 10,"0+2345678R", result 7,"3456789D"
       stored as: 3456 789D

   If all digits are valid, the routine returns no trap, and the converted value
   is present in the target accessor.


   Implementation notes:

    1. Error handling for either conversion requires that the target decimal be
       shortened, so that the resulting memory write matches that produced by
       the microcode.  Shortening a number involves resetting the byte accessor
       length, the starting digit index, and the digit count.  For a left-to-
       right conversion, the leading digits are written first, so setting these
       three values suffices.  For a right-to-left conversion, in addition to
       setting the previous three values, the first byte address, first byte
       offset, and current byte offset must be advanced to the start of the
       first valid digit, and the byte accessor must be set to pick up the new
       values.

    2. Because of the need to return partial results, entry may be made with
       illegal digits present in the source accessor, so they must be detected
       here.  An invalid ASCII sign (overpunch) will have something other than
       an ASCII digit in the last position, so no special handling is needed to
       detect this.

    3. On entry, at least one digit is to be converted, so special testing for a
       zero count is not required.
*/

static uint32 convert_decimal (DIGIT_ACCESS *target, DIGIT_ACCESS *source)
{
HP_BYTE byte;
uint32  index, blank_index, bytes_skipped, byte_count, digit_count;
uint32  trap = trap_None;

if (target->format == Packed) {
    index = MAX_DIGITS;                                 /* work right-to-left */
    blank_index = NOT_SET;                              /* initialize the blank index */

    target->sign = source->sign;                        /* the value adopts the source sign */
    target->significant_index = NOT_SET;                /* recalculate the significant digit index */

    while (index > source->starting_index               /* while there are ASCII digits to convert */
      && index > target->starting_index) {              /*   and packed digits to fill */
        byte = source->digits [--index];                /*     get the next source character */

        if (byte >= '0' && byte <= '9'                  /* if the character is numeric */
          && blank_index == NOT_SET)                    /*   and blanks are not being skipped */
            byte = byte - '0';                          /*     then convert to BCD */

        else if (byte == ' ') {                         /* otherwise if it's a blank */
            byte = 0;                                   /*   then fill with a zero */

            if (blank_index == NOT_SET)                 /* if the blank index has not been set */
                blank_index = index;                    /*   then set it now */
            }

        else {                                          /* otherwise */
            trap = trap_Invalid_ASCII_Digit;            /*   the digit is invalid */
            break;                                      /*     so quit at this point */
            }

        if (byte > 0)                                   /* if the digit is significant */
            target->significant_index = index;          /*   then set or reset the index */

        target->digits [index] = byte;                  /* add the digit to the target */
        }

    if (trap != trap_None) {                            /* if a bad digit is present */
        byte_count = (MAX_DIGITS - index) / 2;          /*   then get the count of good bytes including the sign */

        if (byte_count == 0) {                          /* if all bytes are bad, i.e., a bad sign */
            target->bac.length = 0;                     /*   then skip the write */
            target->significant_index = 0;              /*     but force CCG to match the microcode */
            }

        else {                                                              /* otherwise adjust for full word writes */
            if (target->bac.initial_byte_offset + target->bac.length & 1)   /* if the target ends on an even byte */
                byte_count = byte_count - 1 | 1;                            /*   then adjust the byte count */
            else                                                            /* otherwise it ends on an odd byte */
                byte_count = byte_count & ~1;                               /*   so adjust accordingly */

            bytes_skipped = target->bac.length - byte_count;    /* get the number of bytes that will be skipped */

            target->bac.length = byte_count;                    /* reset the number of bytes to write */
            target->bac.first_byte_address += bytes_skipped;    /*   and move the byte address and offset */
            target->bac.first_byte_offset += bytes_skipped;     /*     forward to the new starting byte */

            digit_count = byte_count * 2 - 1;           /* get the number of digits to write, excluding the sign */

            target->byte_offset += bytes_skipped;               /* move the working offset forward */
            target->starting_index = MAX_DIGITS - digit_count;  /* reset the starting index */
            target->digit_count = digit_count;                  /*   and the count of digits to write */

            mem_set_byte (&target->bac);                /* set the new write location in the target accessor */
            }
        }
    }

else if (target->format == External) {
    if (CIR & NABS_FLAG                                 /* if the request is for an unsigned result */
      || CIR & ABS_FLAG && source->sign != Negative)    /*   or unsigned unless negative */
        target->sign = Unsigned;                        /*     then reset the result sign */
    else                                                /* otherwise */
        target->sign = source->sign;                    /*   the target adopts the source sign */

    index = source->starting_index;                         /* start with the first digit */
    target->significant_index = source->significant_index;  /*   and set the significance index */

    do                                                  /* convert packed decimal to external decimal */
        if (source->digits [index] <= 9)                            /* if the source digit is valid */
            target->digits [index] = source->digits [index] + '0';  /*   then convert it to a character */

        else {                                                      /* otherwise */
            target->bac.length = index - source->starting_index;    /*   reset the operand length */
            target->digit_count = target->bac.length;               /*     to the count of good digits */
            target->sign = Unsigned;                                /*       and omit the sign overpunch */

            trap = trap_Invalid_Decimal_Digit;          /* trap for the error */
            break;                                      /*   and stop the conversion */
            }

    while (++index < MAX_DIGITS);                       /* loop until all digits are converted */
    }

return trap;                                            /* return the trap status */
}


/* Convert between binary and decimal number formats.

   This routines converts a packed decimal number into its multi-word twos
   complement binary equivalent or converts a binary number into its packed
   decimal equivalent.  The direction of the conversion is specified by the LSB
   of the machine instruction in the CIR: 0 for binary-to-decimal and 1 for
   decimal-to-binary.

   On entry, the "decimal" parameter contains an initialized digit accessor that
   is set up to provide (decimal-to-binary) or hold (binary-to-decimal) the
   packed decimal number.  The "address" and "count" parameters specify the
   memory address of the multi-word array used as a corresponding target or
   source for the binary number.  The size of the array depends on the number of
   digits in the packed decimal number, as follows:

     Digits  Words
     ------  -----
      1-4      1
      5-9      2
     10-18     4
     19-28     6

   The binary number is a twos complement value with the most-significant word
   first in the array.

   On return, the converted binary number has been written to memory
   (decimal-to-binary) or the converted decimal number is present in the digit
   accessor.  The function returns a trap value if an error occurred.

   Conversion from decimal to binary proceeds from the LSD to the MSD of the
   decimal value, forming a binary word from each group of four decimal digits
   (resulting in a value from 0-9999), multiplying the multi-word binary value
   by 10,000, and adding in the latest conversion.  This process is repeated
   until all of the significant digits in the source decimal are exhausted.  The
   multi-word multiplication is performed by doing 16 x 16 = 32 bit multiplies,
   saving the lower 16 bits as the current product word, and using the upper 16
   bits as a carry to the next product word.

   After the value conversion is complete, the binary result is negated if the
   source decimal sign is negative; this is done by a complement and increment
   across the array words.

   Conversion from binary to decimal begins by negating the binary number if it
   is negative to obtain the absolute value.  The number is then divided by
   10,000, with the remainder forming the least significant four digits of the
   decimal number.  The division is repeated, obtaining each set of four digits
   in turn, until the dividend is zero or the maximum number of decimal digits
   is reached.  If the limit is reached and the dividend is not yet zero, then a
   decimal overflow trap is indicated.


   Implementation notes:

    1. The CVDB microcode checks for a target bounds violation before reading
       the decimal number or writing any of the target words.  The diagnostic
       checks for this, so we must call "read_decimal" here instead of reading
       the number before calling this routine.  The diagnostic does not test for
       negative numbers, overflows, or bad digits, so these were tested
       independently.

    2. The CVBD microcode checks the upper bound of the binary array against SM
       and not SM + SR as is usually done; see page 172 of the microcode manual.
       The simulation follows the microcode.

    3. When converting from decimal to binary, the starting index into the digit
       array must be adjusted to start on a four-digit boundary, so that each
       group represents a division by 10,000.
*/

static uint32 convert_binary (DIGIT_ACCESS *decimal, HP_WORD address, HP_WORD count)
{
static const char *const binary_formats [] = {          /* binary display formats, indexed by word count */
    BOV_FORMAT "%s %d, %06o\n",                         /*   1 word format */
    BOV_FORMAT "%s %d, %06o %06o\n",                    /*   2 word format */
    BOV_FORMAT "%s %d, %06o %06o %06o\n",               /*   3 word format */
    BOV_FORMAT "%s %d, %06o %06o %06o %06o\n",          /*   4 word format */
    BOV_FORMAT "%s %d, %06o %06o %06o %06o %06o\n",     /*   5 word format */
    BOV_FORMAT "%s %d, %06o %06o %06o %06o %06o %06o\n" /*   6 word format */
    };

HP_WORD binary [6], carry;
uint32  index, counter, digit, offset, end, word_count;
uint32  complement, remainder, accumulator, dividend, sum, partial;
uint32  trap = trap_None;

if (CIR & 1) {                                          /* if this is a decimal-to-binary conversion */
    if (count < 5)                                      /*   then determine */
        word_count = 1;                                 /*     the number of */
    else if (count < 10)                                /*       binary words */
        word_count = 2;                                 /*         needed to hold */
    else if (count < 19)                                /*           the decimal number */
        word_count = 4;                                 /*             that is to be */
    else                                                /*               converted */
        word_count = 6;

    offset = DB + address & LA_MASK;                    /* get the starting and ending */
    end = offset + word_count - 1 & LA_MASK;            /*   memory offsets of the binary array */

    if (NPRV && (offset < DL || end > SM))              /* if non-privileged and out of range */
        MICRO_ABORT (trap_Bounds_Violation);            /*   then trap for a bounds violation */
    else                                                /* otherwise */
        trap = read_decimal (decimal);                  /*   read the source decimal number */

    if (TRACING (cpu_dev, DEB_MOPND))
        fprint_decimal_operand (decimal, "source");

    if (trap == trap_None) {                            /* if the source decimal is valid */
        memset (binary, 0, sizeof binary);              /*   then zero the target array */

        if (decimal->significant_index != NOT_SET) {        /* if the source decimal is not zero */
            index = (decimal->significant_index / 4) * 4;   /*   then point at the first group of four digits */

            do {                                        /* convert groups of four digits to binary */
                sum = 0;                                /* clear the group sum */

                for (counter = 0; counter < 4; counter++)       /* sum the next four */
                    sum = sum * 10 + decimal->digits [index++]; /*   decimal digits */

                carry = sum;                            /* set up the carry into the LSW */
                counter = word_count;                   /*   and start at the end of the array */

                do {                                                /* multiply the binary number by 10,000 */
                    partial = binary [--counter] * 10000 + carry;   /*   and add the sum */

                    binary [counter] = LOWER_WORD (partial);
                    carry            = UPPER_WORD (partial);
                    }
                while (counter > 0);
                }
            while (index < MAX_DIGITS);                 /* loop until all digits are converted */

            if (decimal->sign == Negative) {            /* if the decimal number is negative */
                carry = 1;                              /*   then negate the words */
                counter = word_count;                   /*     in the binary array */

                do {                                    /* perform a twos complement of the binary number */
                    complement       = LOWER_WORD (~binary [--counter]) + carry;
                    binary [counter] = LOWER_WORD (complement);
                    carry            = UPPER_WORD (complement);
                    }
                while (counter > 0);
                }
            }

        tprintf (cpu_dev, DEB_MOPND, binary_formats [word_count - 1],
                 DBANK, offset, address, "  target", word_count,
                 binary [0], binary [1], binary [2],
                 binary [3], binary [4], binary [5]);

        for (index = 0; index < word_count; index++)            /* write the binary number to memory */
            cpu_write_memory (data, offset++, binary [index]);  /*   with checking already done above */
        }
    }

else {                                                  /* otherwise this is a binary-to-decimal conversion */
    offset = DB + address & LA_MASK;                    /* get the starting and ending */
    end = offset + count - 1 & LA_MASK;                 /*   memory offsets of the binary array */

    if (NPRV && (offset < DL || end > SM))              /* if non-privileged and out of range */
        MICRO_ABORT (trap_Bounds_Violation);            /*   then trap for a bounds violation */

    for (index = 0; index < count; index++)                         /* load the binary array */
        cpu_read_memory (data, offset + index, &binary [index]);    /*   with checking already done above */

    tprintf (cpu_dev, DEB_MOPND, binary_formats [count - 1],
             DBANK, offset, address, "  source", count,
             binary [0], binary [1], binary [2],
             binary [3], binary [4], binary [5]);

    if (binary [0] & D16_SIGN) {                        /* if the source binary number is negative */
        decimal->sign = Negative;                       /*   then set the target decimal sign */
        carry = 1;                                      /*     and negate the words */
        counter = count;                                /*       in the array */

        do {                                            /* perform a twos complement of the binary number */
            complement       = LOWER_WORD (~binary [--counter]) + carry;
            binary [counter] = LOWER_WORD (complement);
            carry            = UPPER_WORD (complement);
            }
        while (counter > 0);
        }

    else                                                /* otherwise the binary number is positive */
        decimal->sign = Positive;                       /*   so set the decimal sign */

    decimal->significant_index = NOT_SET;               /* clear the significance counter for CCE detection */
    index = MAX_DIGITS;                                 /*   and start the conversion from the right end */

    do {                                                /* convert the binary array to decimal by decades */
        remainder = 0;                                  /* clear the initial remainder */
        accumulator = 0;                                /*   and the zero accumulator */

        for (counter = 0; counter < count; counter++) { /* divide the binary number by 10,000 */
            dividend = TO_DWORD (remainder, binary [counter]);

            remainder        = dividend % 10000;        /* divide the number by 10,000 */
            binary [counter] = dividend / 10000;        /*   to isolate groups of four digits */

            accumulator |= binary [counter];            /* accumulate to detect when the dividend is zero */
            }

        for (digit = 0; digit < 4; digit++) {           /* split the remainder */
            decimal->digits [--index] = remainder % 10; /*   into four separate digits */
            remainder                 = remainder / 10; /*     and store in the decimal number */

            if (decimal->digits [index] > 0)            /* if the digit is non-zero */
                decimal->significant_index = index;     /*   then (re)set the significance index */
            }
        }
    while (index > 0 && accumulator > 0);               /* loop until out of (significant) digits */

    if (accumulator > 0)                                /* if more digits are present than will fit */
        trap = trap_Decimal_Overflow;                   /*   then set up for an overflow trap */
    }

return trap;
}


/* Read a pair of decimal operands from memory.

   The two decimal operands specified by the four top-of-stack values are read
   into the supplied digit accessors.  The first accessor is read from the
   packed decimal number designated by RA (count) and RB (address) and the
   second from the number designated by RC (count) and RD (address).  If either
   digit count is too large, the routine returns a Word Count Overflow trap.  If
   either number contains an invalid digit, the routine returns an Invalid
   Decimal Digit trap.  The function returns TRUE if the accessors were
   populated with valid numbers and FALSE otherwise.

   If operand tracing is enabled, the two operand values are printed on the
   trace log before returning.
*/

static t_bool read_operands (DIGIT_ACCESS *first, DIGIT_ACCESS *second, uint32 *trap)
{
if (RA > MAX_DIGITS || RC > MAX_DIGITS) {               /* if the operand digit counts are too large */
    *trap = trap_Invalid_Decimal_Length;                /*   then trap for a count overflow */
    return FALSE;                                       /*     and indicate read failure */
    }

else if (RA == 0 || RC == 0) {                          /* otherwise if there are digits to process */
    *trap = trap_None;                                  /*   then indicate read failure */
    return FALSE;                                       /*     without a trap */
    }

else {                                                      /* otherwise there are digits to process */
    init_decimal (first, Packed, data_checked, RB, RA);     /*   so set up the digit accessors */
    init_decimal (second, Packed, data_checked, RD, RC);    /*     for the decimal operands */

    *trap = read_decimal (first);                       /* read the first decimal operand */

    if (TRACING (cpu_dev, DEB_MOPND))
        fprint_decimal_operand (first, "operand-1");

    if (*trap == trap_None) {                           /* if the first decimal is valid */
        *trap = read_decimal (second);                  /*   then read the second decimal operand */

        if (TRACING (cpu_dev, DEB_MOPND))
            fprint_decimal_operand (second, "operand-2");
        }

    return (*trap == trap_None);                        /* return TRUE if both reads were good */
    }
}


/* Write a decimal operand to memory.

   The packed decimal number specified by the supplied digit accessor is written
   to memory, and the condition code is set according to the value.  The
   accessor is reset before writing, in case it was used to read a decimal value
   earlier (e.g., in-place update is being done).  If operand tracing is
   enabled, the operand value is printed on the trace log before returning.
*/

static void write_operand (DIGIT_ACCESS *operand)
{
mem_reset_byte (&operand->bac);                         /* reset the accessor in case it has been used */
write_decimal (operand, TRUE);                          /*   and write the operand to memory */

if (TRACING (cpu_dev, DEB_MOPND))
    fprint_decimal_operand (operand, "result");

set_cca_decimal (operand);                              /* set CCA on the decimal result */

return;
}


/* Set Condition Code A for a decimal number.

   The condition code in the status register is set to reflect the value passed
   in the supplied digit accessor.
*/

static void set_cca_decimal (DIGIT_ACCESS *dap)
{
if (dap->significant_index == NOT_SET)                  /* if the number has no significant digits */
    SET_CCE;                                            /*   then the value is zero */
else if (dap->sign == Negative)                         /* otherwise if the sign is negative */
    SET_CCL;                                            /*   then the value is less than zero */
else                                                    /* otherwise */
    SET_CCG;                                            /*   the value is greater than zero */

return;
}


/* Decrement the stack and trap if enabled.

   This routine is called with a trap value and either two or three stack
   decrement values.  If no trap is indicated or traps are disabled, the stack
   is decremented as specified.  If two values are specified, indicated by the
   third decrement value being zero, the stack decrement bit is examined to
   select either the first (0) or second (1) decrement value.  If three values
   are specified, then the stack decrement field (two bits) is examined, and the
   field value determines the stack decrement value to use.  For example:

     Parameters   CIR Test   Decrements
     ----------  ----------  ----------
      1, 3, 0      bit 11      -1, -3
      0, 2, 0      bit 11       0, -2
      0, 2, 4    bits 10-11   0, -2, -4

   After the decrement is performed, if a trap is indicated, a microcode abort
   is done.  Otherwise, the overflow register is cleared, and the routine
   returns.
*/

static void decrement_stack (uint32 trap, uint32 count_0, uint32 count_1, uint32 count_2)
{
uint32 count, decrement;

if (trap == trap_None || (STA & STATUS_T) == 0) {       /* if the instruction succeeded or user traps are disabled */
    if (count_2 == 0)                                   /*   then if only two choices are present */
        if (CIR & EIS_SDEC_FLAG)                        /*     then if the S-decrement flag is set */
            decrement = count_1;                        /*       then decrement by the second choice */
        else                                            /*     otherwise */
            decrement = count_0;                        /*       then decrement by the first choice */

    else switch (EIS_SDEC (CIR)) {                      /* otherwise select among the three choices */

        case 0:                                         /* if the S-decrement field is 00 */
            decrement = count_0;                        /*   then select the first choice */
            break;

        case 1:                                         /* if the S-decrement field is 01 */
            decrement = count_1;                        /*   then select the second choice */
            break;

        case 2:                                         /* if the S-decrement field is 10 */
        default:                                        /*   or 11 (invalid) */
            decrement = count_2;                        /*   then select the third choice */
            break;
        }

    if (decrement == 4)                                 /* if four parameters are to be deleted */
        SR = 0;                                         /*   then simply clear the stack counter */

    else for (count = 0; count < decrement; count++)    /* otherwise delete the number */
        cpu_pop ();                                     /*   of items requested */
    }

if (trap == trap_None)                                  /* if the instruction succeeded */
    STA &= ~STATUS_O;                                   /*   then clear overflow status */
else                                                    /* otherwise */
    MICRO_ABORT (trap);                                 /*   abort with the indicated trap */

return;
}


/* Strip the sign from an overpunched digit.

   If the supplied character includes a valid overpunched sign, it is stripped
   and returned separately, the character is set to the stripped digit, and
   trap_None is returned to indicate success.  If the character is not a valid
   overpunch character, then trap_Invalid_ASCII_Digit is returned to indicate
   failure.


   Implementation notes:

    1. A set of direct tests is faster than looping through the overpunch table
       to search for the matching digit.  Faster still would be a direct 256-way
       reverse overpunch lookup, but the gain is not significant.
*/

static uint32 strip_overpunch (HP_BYTE *byte, NUMERIC_SIGN *sign)
{
if (*byte == '{') {                                     /* if the digit is a zero with positive overpunch */
    *byte = '0';                                        /*   then strip the overpunch */
    *sign = Positive;                                   /*     and set the returned sign positive */
    }

else if (*byte >= 'A' && *byte <= 'I') {                /* otherwise if the digit is a positive overpunch */
    *byte = *byte - 'A' + '1';                          /*   then strip the overpunch to yield a 1-9 value */
    *sign = Positive;                                   /*     and set the returned sign positive */
    }

else if (*byte == '}') {                                /* otherwise if the digit is a zero with negative overpunch */
    *byte = '0';                                        /*   then strip the overpunch */
    *sign = Negative;                                   /*     and set the returned sign negative */
    }

else if (*byte >= 'J' && *byte <= 'R') {                /* otherwise if the digit is a negative overpunch */
    *byte = *byte - 'J' + '1';                          /*   then strip the overpunch to yield a 1-9 value */
    *sign = Negative;                                   /*     and set the returned sign negative */
    }

else if (*byte >= '0' && *byte <= '9')                  /* otherwise if the digit is not overpunched */
    *sign = Unsigned;                                   /*   then simply set the return as unsigned */

else                                                    /* otherwise the digit is not a valid overpunch */
    return trap_Invalid_ASCII_Digit;                    /*   so return an Invalid Digit trap as failure */

return trap_None;                                       /* return no trap for successful stripping */
}


/* Format and print a decimal memory operand.

   The decimal operand described by the decimal accessor is sent to the debug
   trace log file.  Operand tracing must be enabled when the routine is called.

   On entry, "op" points at the decimal accessor describing the operand, and
   "label" points to text used to label the operand.

   The operand is printed in this format:

     >>CPU  opnd: 00.045177  000467    source 15,"314159265358979"
                  ~~ ~~~~~~  ~~~~~~    ~~~~~~ ~~ ~~~~~~~~~~~~~~~~~
                  |    |       |         |    |          |
                  |    |       |         |    |          +-- operand value
                  |    |       |         |    +------------- operand length
                  |    |       |         +------------------ operand label
                  |    |       +---------------------------- octal relative byte offset from base register
                  |    +------------------------------------ octal operand address (effective address)
                  +----------------------------------------- octal operand bank (PBANK, DBANK, or SBANK)
*/

static void fprint_decimal_operand (DIGIT_ACCESS *op, char *label)
{
typedef char * (*OP_PRINT) (uint32 address, uint32 length);

OP_PRINT operand_printer;

if (op->format == Packed)                               /* if this is a packed decimal number */
    operand_printer = fmt_bcd_operand;                  /*   then use the BCD operand printer */
else                                                    /* otherwise */
    operand_printer = fmt_byte_operand;                 /*   use the character operand printer */

hp_trace (&cpu_dev, DEB_MOPND, BOV_FORMAT "  %s %d,\"%s\"\n",
          TO_BANK (op->bac.first_byte_address / 2),
          TO_OFFSET (op->bac.first_byte_address / 2),
          op->bac.first_byte_offset, label, op->digit_count,
          operand_printer (op->bac.first_byte_address, op->digit_count));

return;
}
