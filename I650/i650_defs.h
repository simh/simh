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
#define STOP_IO         4               /* Stop on IO: 
                                             card reader/punch error: 
                                               no card in hopper, read/punch failure, no cards, stop pressed on cdr/cdp
                                               only simulated no card in hopper situation when all cards from attached file has been read
                                             tape:
                                               executed tape opcode and got non handled by indicator error
                                             disk:
                                               selected arm or unit out of range
                                        */
#define STOP_PROG       5               /* Programmed stop */
#define STOP_OV         6               /* Overflow stop */
#define STOP_ERRO       7               /* Error in opcode execution: BRD in witch position tested not 8 or 9, TLU failure */
#define STOP_ADDR       8               /* Address stop: Store attempt on addr 800X, address out of drum mem */


/* Memory */
#define MAXDRUMSIZE      (4000)
#define DRUMSIZE         ((int)(cpu_unit.capac % 10) * 1000)          /* actual drum memory size */

extern t_int64          DRUM[MAXDRUMSIZE];
extern int              DRUM_NegativeZeroFlag[MAXDRUMSIZE];
extern char             DRUM_Symbolic_Buffer[MAXDRUMSIZE * 80];
extern char             IAS_Symbolic_Buffer[60 * 80];

extern t_int64          IOSync[10];
extern int              IOSync_NegativeZeroFlag[10];

#define STOR            ((uint32)cpu_unit.flags & OPTION_STOR)                // return non zero if set cpu storage option set
#define CNTRL           ((uint32)cpu_unit.flags & OPTION_CNTRL)               // return non zero if set cpu cntrl option set
#define FAST            ((uint32)(cpu_unit.flags & OPTION_FAST) ? 1:0)        // return non zero if set cpu fast option set
#define DRUM4K          ((uint32)cpu_unit.flags & MEMAMOUNT(2))               // return 0 if drum size < 4k, non zero if = 4k

extern t_int64          IAS[60];
extern int              IAS_NegativeZeroFlag[60];
extern int              IAS_TimingRing;
extern int              InterLockCount[8];

extern int WriteAddr(int AR, t_int64 d, int NegZero);
extern int ReadAddr(int AR, t_int64 * d, int * NegZero);
extern CONST char * DecodeOpcode(t_int64 d, int * opcode, int * DA, int * IA);


/* digits contants */
#define D10            (10000000000LL)      // ten digits (10 zeroes)
#define D8                (100000000L)      // eight digits (8 zeroes)
#define D4                    (10000L)      // four digits (4 zeroes)

// increment umber of word counts elapsed from starting of simulator -> this is the global time measurement
extern t_int64 GlobalWordTimeCount;

/* Device information block */
struct dib {
        uint8   upc;                        // Number of Units in device 
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

/* Returns from device commands */
#define SCPE_BUSY           (1)     // Device is active 
#define SCPE_NOCARDS        (2)     // No cards to read or to write 
#define SCPE_OK_INPROGRESS  (3)     // Operation in progress

/* Global device definitions */
#ifdef CPANEL
extern DEVICE       cp_dev;
#endif

#define MAX_CARDS_IN_DECK  10000            // max number of cards in deck for carddeck internal command
#define MAX_CARDS_IN_READ_STAKER_HOPPER 10  // max number of cards in card reader take 
                                            // staker that can be viewev with carddeck echolast

extern DIB          cdr_dib;
extern DEVICE       cdr_dev;
extern uint32       cdr_cmd(UNIT *, uint16, uint16);
extern UNIT         cdr_unit[4];
extern uint16       ReadStaker[3 * MAX_CARDS_IN_READ_STAKER_HOPPER * 80];
extern int          ReadStakerLast[3];

extern DIB          cdp_dib;
extern DEVICE       cdp_dev;
extern uint32       cdp_cmd(UNIT *, uint16, uint16);
extern UNIT         cdp_unit[4];

/* Card read-punch device status information stored in u5 */
#define URCSTA_ERR       0002    /* Error reading record */
#define URCSTA_BUSY      0010    /* Device unit is busy */
#define URCSTA_LOAD     01000    /* Load flag for 533 card reader */
#define URCSTA_SOAPSYMB 02000    /* Get soap symbolic info when reading the card */

extern DIB          mt_dib;
extern DEVICE       mt_dev;
extern uint32       mt_cmd(UNIT *, uint16, uint16);
extern UNIT         mt_unit[6];
extern int          LastTapeSelected;
extern int          LastTapeIndicator;    
extern const char * TapeIndicatorStr[11];
extern int          mt_ready(int n);
extern void         mt_ini(UNIT * uptr, t_bool f);

/* Tape Indicator status */
#define MT_IND_WRT_PROT    1    // attempting to write to a write protected tape
#define MT_IND_IOCHECK     2    // host os i/o error on tape file
#define MT_IND_EOF         3    // found Tape Mark in current record while reading
#define MT_IND_EOT         4    // found End of Tape Mark while reading/writing
#define MT_IND_LONG_REC    5    // record begin read from tape does not fit in record defined at IAS storage
#define MT_IND_SHORT_REC   6    // record begin read from tape does not fill record defined at IAS storage
#define MT_IND_DIS         7    // no tape has this address (tape unit is disabled)
#define MT_IND_NOATT       8    // no reel load on tape (no tape file attached)
#define MT_IND_NOTRDY      9    // tape not ready
#define MT_IND_BADCHAR    10    // tape not ready

extern DIB          dsk_dib;
extern DEVICE       dsk_dev;
extern uint32       dsk_cmd(int, int32, uint16);
extern UNIT         dsk_unit[4];
extern int          dsk_ready(int n, int arm);
extern void         dsk_ini(UNIT * uptr, t_bool f);

/* Disk Indicator status */
#define DSK_IND_BADADDR     1    // invalid unit/arm/disk plate/track accessed
#define DSK_IND_IOCHECK     2    // host os i/o error on disk file
#define DSK_IND_DIS         7    // no disk has this address (disk unit is disabled)
#define DIS_IND_NOATT       8    // no disk file attached
#define DIS_IND_NOTRDY      9    // disk arm not ready

extern struct card_wirings {
    uint32      mode;
    const char  *name;
} wirings[];

extern char    digits_ascii[31];
extern char    mem_to_ascii[101];
extern int     ascii_to_NN(int ch);
extern uint16  sim_ascii_to_hol(char c);
extern char    sim_hol_to_ascii(uint16 hol);

/* Generic devices common to all */
extern DEVICE      cpu_dev;
extern UNIT        cpu_unit;
extern REG         cpu_reg[];

extern const char *cpu_description(DEVICE *dptr);

/* Opcodes */
// Instructions on Basic machine 
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
#define OP_DIV     14   // Divide 
#define OP_DIVRU   64   // Divide and reset upper accumulator 
#define OP_LD      69   // Load distributor 
#define OP_MULT    19   // Multiply 
#define OP_NOOP    00   // No operation 
#define OP_PCH     71   // Punch a card 
#define OP_RD      70   // Read a card 
#define OP_RAABL   67   // Reset accumulator and add absolute to lower accumulator 
#define OP_RAL     65   // Reset accumulator and add to lower accumulator 
#define OP_RAU     60   // Reset accumulator and add to upper accumulator 
#define OP_RSABL   68   // Reset accumulator and subtract absolute from lower accumulator 
#define OP_RSL     66   // Reset accumulator and subtract from lower accumulator 
#define OP_RSU     61   // Reset accumulator and subtract from upper accumulator 
#define OP_SLT     35   // Shift accumulator left 
#define OP_SCT     36   // Shift accumulator left and count  
#define OP_SRT     30   // Shift accumulator right 
#define OP_SRD     31   // Shift accumulator right and round accumulator 
#define OP_STOP    01   // Stop if console switch is set to stop, otherwise continue as a NO-OP 
#define OP_STD     24   // Store distributor into memory 
#define OP_STDA    22   // Store lower accumulator data address into distributor, then store distributor into memory
#define OP_STIA    23   // Store lower accumulator instruction address into distributor, then store distributor into memory
#define OP_STL     20   // Store lower accumulator into memory 
#define OP_STU     21   // Store upper accumulator into memory 
#define OP_SABL    18   // Subtract absolute from lower accumulator 
#define OP_SL      16   // Subtract from lower accumulator 
#define OP_SU      11   // Subtract from upper accumulator 
#define OP_TLU     84   // Table lookup 
#define OP_TLE     63   // Table lookup on equal
// Instructions on Storage Unit 
// opcodes for indexing
#define OP_AXA     50   // Add to index register A
#define OP_SXA     51   // Substract from index A
#define OP_RAA     80   // Reset Add Index A
#define OP_RSA     81   // Reset Substract Index A
#define OP_NZA     40   // Branch Non Zero Index A
#define OP_BMA     41   // Branch Minus Index A
#define OP_AXB     52   // Add to index register B
#define OP_SXB     53   // Substract from index B
#define OP_RAB     82   // Reset Add Index B
#define OP_RSB     83   // Reset Substract Index B
#define OP_NZB     42   // Branch Non Zero Index B
#define OP_BMB     43   // Branch Minus Index B
#define OP_AXC     58   // Add to index register C
#define OP_SXC     59   // Substract from index C
#define OP_RAC     88   // Reset Add Index C
#define OP_RSC     89   // Reset Substract Index C
#define OP_NZC     48   // Branch Non Zero Index C
#define OP_BMC     49   // Branch Minus Index C
// io for synchronizers 2 & 3
#define OP_RC1     72   // Read Conditional sync 1
#define OP_RD2     73   // Read Sync 2
#define OP_WR2     74   // Write Sync 2
#define OP_RC2     75   // Read Conditional Sync 2
#define OP_RD3     76   // Read Sync 3
#define OP_WR3     77   // Write Sync 3
#define OP_RC3     78   // Read Conditional Sync 3
// immediate access storage (ias)
#define OP_LIB      8   // Load IAS block
#define OP_LDI      9   // Load IAS
#define OP_SIB     28   // Store IAS Block
#define OP_STI     29   // Store IAS
#define OP_SET     27   // Set IAS Timing Ring
// floating point
#define OP_FAD     32   // Floating Add
#define OP_FSB     33   // Floating Subtract
#define OP_FMP     39   // Floating Multiply
#define OP_FDV     34   // Floating Divide
#define OP_UFA     02   // Unnormalized Floating Add
#define OP_FAM     37   // Floating Add Absolute (Magnitude)
#define OP_FSM     38   // Floating Subtract Absolute (Magnitude)
// Instructions on Control Unit
// tape
#define OP_RTN     04   // Read Tape Numeric
#define OP_RTA     05   // Read Tape Alphameric
#define OP_WTN     06   // Write Tape Numeric
#define OP_WTA     07   // Write Tape Alphameric
#define OP_RTC     03   // Read Tape for Checking
#define OP_NTS     25   // Branch no Tape Signal
#define OP_NEF     54   // Branch no End of File
#define OP_RWD     55   // Rewind Tape
#define OP_WTM     56   // Write Tape Mark
#define OP_BST     57   // Backspace Tape
// ramac disk
#define OP_SDS     85   // Seek Disk Storage
#define OP_RDS     86   // Read Disk Storage
#define OP_WDS     87   // Write Disk Storage
// inquiry stations
#define OP_BIN     26   // Branch on Inquiry
#define OP_RPY     79   // Reply on Inquiry

// Valid Data Address (DA) 
#define  vda_D      1   // 0000-1999    Drum
#define  vda_A      2   // 8000-8003    Arithmetic unit registers (ACC Low & Hi), Distributor, Console Switches register 
#define  vda_I      4   // 8005-8007    Index Registers (IR)
#define  vda_T      8   // 8010-8015    Tape address
#define  vda_S     16   // 9000-9059    Immediate Access Storage (IAS)
#define  vda_9000  32   // 9000         Only addr 9000 valid 

#define  vda_DAITS    (vda_D | vda_A | vda_I | vda_T | vda_S ) 
#define  vda_DAIS     (vda_D | vda_A | vda_I |         vda_S ) 
#define  vda_DAS      (vda_D | vda_A |                 vda_S ) 
#define  vda_DS       (vda_D |                         vda_S ) 

#define  opReadDA        1   // opcode fetchs data from DA address
#define  opWriteDA       2   // opcode write data to DA

#define  opStorUnit      1  // opcode available if IBM 653 Storage Unit is present
#define  opCntrlUnit     2  // opcode available if IBM 652 Control Unit is present
#define  opTLE           3  // opcode available if Table Lookup on equal feature installed

#define IL_RD1           1  // interlock on drum area 01-10/51-60 used in reading with RD1
#define IL_WR1           2  // interlock on drum area 27-36/77-86 used in writing for WR1
#define IL_RD23          3  // interlock on drum area 39-48/89-98 used in reading with RD2/RD3
#define IL_WR23          4  // interlock on drum area 13-22/63-72 used in writing for WR2/WR3
#define IL_IAS           5  // interlock on ias access
#define IL_Tape          6  // interlock on tape control circuits
#define IL_RamacUnit     7  // interlock on ramac unit control circuits

#define IL_Tape_and_Unit_and_IAS        100         // interlock IAS + Tape control + Tape Unit
#define IL_Tape_and_Unit                101         // interlock Tape control + Tape Unit
#define IL_RamacUnit_and_Arm_and_IAS    102         // interlock IAS + Ramac unit control + Unit Access Arm
#define IL_RamacUnit_and_Arm            103         // interlock + Ramac unit control + Unit Access Arm

#define msec_to_wordtime(n)     ((int)(n / 0.096))      // convert time in msec to number of word times
#define msec_elapsed(n)        ((int)((GlobalWordTimeCount - (n)) * 0.096))  // return msec elapsed from a give wordtime stamp


/* Symbol tables */
typedef struct 
{
    uint16              opbase;         // opcode number
    const char         *name1;          // opcode name as in operation manual
    const char         *name2;          // opcode name as in soap 
    uint8               opRW;           // =wDA, rDA or zero
    int                 option;         // =0 -> opcode in basic machine, =1 -> Opcode because Storage Unit, =2 -> Opcode because Control Unit
    int                 validDA;        // valid data address for this instruction
    int                 opInterLock;    // Interlock required by opcode
}
t_opcode;

extern t_opcode  base_ops[100];


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
#define WIRING_SOAPA       (  0x200 << UNIT_V_CARD_MODE)
#define WIRING_IS          (  0x300 << UNIT_V_CARD_MODE)
#define WIRING_IT          (  0x400 << UNIT_V_CARD_MODE)
#define WIRING_FORTRANSIT  (  0x500 << UNIT_V_CARD_MODE)
#define WIRING_RA          (  0x600 << UNIT_V_CARD_MODE)
#define WIRING_FDS         (  0x700 << UNIT_V_CARD_MODE)
#define WIRING_SUPERSOAP   (  0x800 << UNIT_V_CARD_MODE)
#define UNIT_CARD_ECHO     ( 0x1000 << UNIT_V_CARD_MODE)
#define UNIT_CARD_PRINT    ( 0x2000 << UNIT_V_CARD_MODE)

/* Decimal helper functions */
extern int Get_HiDigit(t_int64 d);
extern int Shift_Digits(t_int64 * d, int nDigits);
extern char * word_to_ascii(char * buf, int CharStart, int CharLen, t_int64 d);

extern void vm_init(void);  /* One time initialization activities now called in cpu_reset() */


