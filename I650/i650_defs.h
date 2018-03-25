/* i650_defs.h: IBM 650 simulator definitions

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

*/

#include "sim_defs.h"                                   /* simulator defns */

/* Simulator stop codes */

#define STOP_HALT       1               /* HALT */
#define STOP_IBKPT      2               /* breakpoint */
#define STOP_UUO        3               /* invalid opcode */
#define STOP_CARD       4               /* Stop on card reader/punch error (no card in hopper, read/punch failure, no cards, stop pressed on cdr/cdp*/
#define STOP_PROG       5               /* Programmed stop */
#define STOP_OV         6               /* Overflow stop */
#define STOP_ERRO       7               /* Error in opcode execution: BRD in witch position tested not 8 or 9, TLU failure */
#define STOP_ADDR       8               /* Address stop: Store attempt on addr 800X, address out of drum mem */


/* Memory */
#define MAXMEMSIZE      (4000)
#define MEMSIZE         cpu_unit.capac          /* actual memory size */
#define MEMMASK         (MEMSIZE - 1)           /* Memory bits */

#define MEM_ADDR_OK(x)  (((uint32) (x)) < MEMSIZE)
extern t_int64          DRUM[MAXMEMSIZE];
extern int              DRUM_NegativeZeroFlag[MAXMEMSIZE];

extern int WriteDrum(int AR, t_int64 d, int NegZero);
extern int ReadDrum(int AR, t_int64 * d, int * NegZero);

/* digits contants */
#define D10            (10000000000LL)      // ten digits (10 zeroes)
#define D8                (100000000L)      // eight digits (8 zeroes)
#define D4                    (10000L)      // four digits (4 zeroes)


/* Device information block */
struct dib {
        uint8   upc;                                    /* Units per channel */
        uint32  (*cmd)(UNIT *up, uint16 cmd, uint16 dev);/* Issue command. */
        void    (*ini)(UNIT *up, t_bool f);
};

typedef struct dib DIB;

/* Debuging controls */
#define DEBUG_CMD       0x0000010       /* Show device commands */
#define DEBUG_DETAIL    0x0000020       /* Show details */
#define DEBUG_EXP       0x0000040       /* Show error conditions */
#define DEBUG_DATA      0x0000080       /* Show data details */

extern DEBTAB dev_debug[];
extern DEBTAB crd_debug[];

/* Returns from read/write */
#define DATA_OK         0       /* Data transfered ok */
#define TIME_ERROR      1       /* Channel did not transfer last operation */
#define END_RECORD      2       /* End of record */

/* Returns from device commands */
#define SCPE_BUSY       (1)     /* Device is active */
#define SCPE_NOCARDS    (2)     /* No cards to read or ti write */

/* Global device definitions */
#ifdef CPANEL
extern DEVICE       cp_dev;
#endif

extern DIB          cdr_dib;
extern DEVICE       cdr_dev;
extern uint32       cdr_cmd(UNIT *, uint16, uint16);
extern UNIT         cdr_unit[];

extern DIB          cdp_dib;
extern DEVICE       cdp_dev;
extern uint32       cdp_cmd(UNIT *, uint16, uint16);
extern UNIT         cdp_unit[];

/* Device status information stored in u5 */
#define URCSTA_ERR      0002    /* Error reading record */
#define URCSTA_BUSY     0010    /* Device is busy */
#define URCSTA_LOAD    01000    /* Load flag for 533 card reader */


/* Character codes in IBM 650 as stated in p4 Andree Programming the IBM 650 Mag Drum 
   Also stated in www.bitsavers.org/pdf/ibm/650/28-4028_FOR_TRANSIT.pdf p37
*/
#define CHR_BLANK       00
#define CHR_DOT         18     // card code: 12-3-8   .
#define CHR_RPARENT     19     //            12-4-8   )
#define CHR_AMPERSAND   20     //            12       +
#define CHR_DOLLAR      28     //            11-3-8   $
#define CHR_STAR        29     //            11-4-8   *
#define CHR_NEG         30     //            11       -    minus sign for negative value
#define CHR_SLASH       31     //            0-1      /
#define CHR_COMMA       38     //            0-3-8    ,
#define CHR_LPARENT     39     //            0-4-8    (
#define CHR_EQUAL       48     //            3-8      =
#define CHR_MINUS       49     //            4-8      -
#define CHR_A           61
#define CHR_B           62
#define CHR_C           63
#define CHR_D           64
#define CHR_E           65
#define CHR_F           66
#define CHR_G           67
#define CHR_H           68
#define CHR_I           69
#define CHR_J           71
#define CHR_K           72
#define CHR_L           73
#define CHR_M           74
#define CHR_N           75
#define CHR_O           76
#define CHR_P           77
#define CHR_Q           78
#define CHR_R           79
#define CHR_S           82
#define CHR_T           83
#define CHR_U           84
#define CHR_V           85
#define CHR_W           86
#define CHR_X           87
#define CHR_Y           88
#define CHR_Z           89
#define CHR_0           90
#define CHR_1           91
#define CHR_2           92
#define CHR_3           93
#define CHR_4           94
#define CHR_5           95
#define CHR_6           96
#define CHR_7           97
#define CHR_8           98
#define CHR_9           99

extern char    digits_ascii[40];
extern char    mem_to_ascii[100];
extern int     ascii_to_NN(int ch);
extern uint16  ascii_to_hol[128];


/* Generic devices common to all */
extern DEVICE      cpu_dev;
extern UNIT        cpu_unit;
extern REG         cpu_reg[];
extern int         cycle_time;

/* I/O Command codes */
#define IO_RDS  1       /* Read record */
#define IO_WRS  4       /* Write one record */


extern const char *cpu_description(DEVICE *dptr);

/* Opcodes */
#define OP_AABL    17  // Add absolute to lower accumulator 
#define OP_AL      15  // Add to lower accumulator 
#define OP_AU      10  // Add to upper accumulator 
#define OP_BRNZ    45  // Branch on accumulator non-zero 
#define OP_BRMIN   46  // Branch on minus accumulator 
#define OP_BRNZU   44  // Branch on non-zero in upper accumulator 
#define OP_BROV    47  // Branch on overflow 
#define OP_BRD1    91  // Branch on 8 in distributor positions 1-10 
#define OP_BRD2    92
#define OP_BRD3    93
#define OP_BRD4    94
#define OP_BRD5    95
#define OP_BRD6    96
#define OP_BRD7    97
#define OP_BRD8    98
#define OP_BRD9    99
#define OP_BRD10   90
#define OP_DIV     14  // Divide 
#define OP_DIVRU   64  // Divide and reset upper accumulator 
#define OP_LD      69  // Load distributor 
#define OP_MULT    19  // Multiply 
#define OP_NOOP    00  // No operation 
#define OP_PCH     71  // Punch a card 
#define OP_RD      70  // Read a card 
#define OP_RAABL   67  // Reset accumulator and add absolute to lower accumulator 
#define OP_RAL     65  // Reset accumulator and add to lower accumulator 
#define OP_RAU     60  // Reset accumulator and add to upper accumulator 
#define OP_RSABL   68  // Reset accumulator and subtract absolute from lower accumulator 
#define OP_RSL     66  // Reset accumulator and subtract from lower accumulator 
#define OP_RSU     61  // Reset accumulator and subtract from upper accumulator 
#define OP_SLT     35  // Shift accumulator left 
#define OP_SCT     36  // Shift accumulator left and count  
#define OP_SRT     30  // Shift accumulator right 
#define OP_SRD     31  // Shift accumulator right and round accumulator 
#define OP_STOP    01  // Stop if console switch is set to stop, otherwise continue as a NO-OP 
#define OP_STD     24  // Store distributor into memory 
#define OP_STDA    22  // Store lower accumulator data address into distributor, then store distributor into memory
#define OP_STIA    23  // Store lower accumulator instruction address into distributor, then store distributor into memory
#define OP_STL     20  // Store lower accumulator into memory 
#define OP_STU     21  // Store upper accumulator into memory 
#define OP_SABL    18  // Subtract absolute from lower accumulator 
#define OP_SL      16  // Subtract from lower accumulator 
#define OP_SU      11  // Subtract from upper accumulator 
#define OP_TLU     84  // Table lookup 

#define NEGZERO_value          0x7fffFFFFffffFFFF
#define AccNegative            (((AccNegativeZeroFlag) || (ACC[1]<0) || (ACC[0]<0)) ? 1:0)
#define AbsWord(d)             ((d < 0) ? -d:d)
#define printfw(d,negzero)     (int32) AbsWord(d/D4), (int32) AbsWord(d%D4), ((d<0) || (negzero)) ? '-':'+'
#define printfd                printfw(DIST, DistNegativeZeroFlag)
#define printfa                (int32) AbsWord(ACC[1]/D4),(int32) AbsWord(ACC[1]%D4), printfw(AbsWord(ACC[0]), AccNegative)

/* Standard control panel wiring for card read/punch/print */

#define UNIT_CARD_WIRING   (  0xF00 << UNIT_V_CARD_MODE)
#define WIRING_8WORD       (  0x000 << UNIT_V_CARD_MODE)
#define WIRING_SOAP        (  0x100 << UNIT_V_CARD_MODE)
#define WIRING_IS          (  0x200 << UNIT_V_CARD_MODE)
#define UNIT_CARD_ECHO     ( 0x1000 << UNIT_V_CARD_MODE)
#define UNIT_CARD_PRINT    ( 0x2000 << UNIT_V_CARD_MODE)

struct card_wirings {
    uint32      mode;
    const char  *name;
};

/* Decimal helper functions */
extern int Get_HiDigit(t_int64 d);
extern int Shift_Digits(t_int64 * d, int nDigits);
extern char * word_to_ascii(char * buf, int CharStart, int CharLen, t_int64 d);


