/* i650_cpu.c: IBM 650 CPU simulator

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

   cpu          IBM 650 central processor

   From Wikipedia: The IBM 650 Magnetic Drum Data-Processing Machine is one of
   IBM's early computers, and the world’s first mass-produced computer. It was
   announced in 1953 and in 1956 enhanced as the IBM 650 RAMAC with the 
   addition of up to four disk storage units. Almost 2,000 systems were 
   produced, the last in 1962.

   The 650 was a two-address, bi-quinary coded decimal computer (both data and
   addresses were decimal), with memory on a rotating magnetic drum. Character 
   support was provided by the input/output units converting punched card 
   alphabetical and special character encodings to/from a two-digit decimal 
   code.

   Rotating drum memory provided 1,000, 2,000, or 4,000 words of memory (a 
   signed 10-digit number or five characters per word) at addresses 0000 to 
   0999, 1999, or 3999 respectively.

   Instructions read from the drum went to a program register (in current 
   terminology, an instruction register). Data read from the drum went through 
   a 10-digit distributor. The 650 had a 20-digit accumulator, divided into 
   10-digit lower and upper accumulators with a common sign. Arithmetic was 
   performed by a one-digit adder. The console (10 digit switches, one sign 
   switch, and 10 bi-quinary display lights), distributor, lower and upper 
   accumulators were all addressable; 8000, 8001, 8002, 8003 respectively.

   The 650 instructions consisted of a two-digit operation code, a four-digit 
   data address and the four-digit address of the next instruction. The sign 
   was ignored on the basic machine, but was used on machines with optional 
   features. The base machine had 44 operation codes. Additional operation 
   codes were provided for options, such as floating point, core storage, 
   index registers and additional I/O devices. With all options installed, 
   there were 97 operation codes.

   The programmer visible system state for the IBM 650 is:

   CSW <10:1>            Console Switches 
   ACC[0] <10:1>         Lower Accumulator register
   ACC[1] <10:1>         Upper Accumulator register
   DIST <10:1>           Distributor
   OV<0:0>               Overflow flag

   The 650 had one basic instuction format.
   Intructions are stores as 10 digits (0-9) words in drum memory

   10 9 | 8 7 6 5 | 4 3 2 1 |  0
   -----+---------+---------+-----
   op   |   Data  |  Instr  | Sign
   code |   Addr  |  Addr

   First two digits are opcodes
   digits 8-5 is data address referenced by opcode
   digits 4-1 is instruction address: address of next instruction

   Instruction support as described in BitSavers 22-6060-2_650_OperMan.pdf

*/

#include "i650_defs.h"

extern const char * get_opcode_data(int opcode, int * ReadData);

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (7 << UNIT_V_MSIZE)
#define UNIT_V_CPUMODEL (UNIT_V_UF + 4)
#define UNIT_MODEL      (0x01 << UNIT_V_CPUMODEL)
#define CPU_MODEL       ((cpu_unit.flags >> UNIT_V_CPUMODEL) & 0x01)
#define MODEL(x)        (x << UNIT_V_CPUMODEL)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
//XXX  #define OPTION_FLOAT    (1 << (UNIT_V_CPUMODEL + 1))

t_stat              cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw);
t_stat              cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw);
t_stat              cpu_reset(DEVICE * dptr);
t_stat              cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat              cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat              cpu_svc (UNIT *uptr);
const char          *cpu_description (DEVICE *dptr);

t_int64             DRUM[MAXMEMSIZE]                        = {0};
int                 DRUM_NegativeZeroFlag[MAXMEMSIZE]       = {0};
char                DRUM_Symbolic_Buffer[MAXMEMSIZE * 80]   = {0}; // does not exists on real hw. Used to keep symbolic info 

// cpu registers
uint16              IC;                          // Added register not part of cpu. Has addr of current intr in execution, just for displaying purposes. IBM 650 has no program counter
t_int64             ACC[2];                      /* lower, upper accumulator. 10 digits (=one word) each*/
t_int64             DIST;                        /* ditributor. 10 digits */
t_int64             CSW = 0;                     /* Console Switches, 10 digits */
t_int64             PR;                          /* Program Register: hold current instr in execution, 10 digits*/
uint16              AR;                          /* Address Register: address references to drum */
uint8               OV;                          /* Overflow flag */
uint8               CSWProgStop     = 1;         /* Console programmed stop switch */
uint8               CSWOverflowStop = 0;         /* Console stop on overflow switch */
uint8 HalfCycle          = 0;                    // set to 0 for normal run, =1 to execute I-Half-cycle, =2 to execute D-half-cycle
int AccNegativeZeroFlag  = 0;                    // set to 1 if acc has a negative zero
int DistNegativeZeroFlag = 0;                    // set to 1 if distributor has a negative zero


/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit =
    { UDATA(&cpu_svc, MEMAMOUNT(0)|MODEL(0x0), 1000), 10  };


REG                 cpu_reg[] = {
    {DRDATAD(IC, IC, 16, "Current Instruction"), REG_FIT},
    {HRDATAD(DIST, DIST, 64, "Distributor"), REG_VMIO|REG_FIT},
    {HRDATAD(ACCLO, ACC[0], 64, "Lower Accumulator"), REG_VMIO|REG_FIT},
    {HRDATAD(ACCUP, ACC[1], 64, "Upper Accumulator"), REG_VMIO|REG_FIT},
    {HRDATAD(PR, PR, 64, "Program Register"), REG_VMIO|REG_FIT},
    {DRDATAD(AR, AR, 16, "Address Register"), REG_FIT},
    {ORDATAD(OV, OV, 1, "Overflow"), REG_FIT},
    {HRDATAD(CSW, CSW, 64, "Console Switches"), REG_VMIO|REG_FIT},
    {ORDATAD(CSWPS, CSWProgStop, 1, "Console Switch Program Stop"), REG_FIT},
    {ORDATAD(CSWOS, CSWOverflowStop, 1, "Console Switch Overflow Stop"), REG_FIT},
    {ORDATAD(HALF, HalfCycle, 2, "Half Cycle"), REG_FIT},
    {NULL}
};

MTAB                cpu_mod[] = {
    {UNIT_MSIZE, MEMAMOUNT(0), "1K", "1K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(1), "2K", "2K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(2), "4K", "4K", &cpu_set_size},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 10, 12, 1, 10, 64,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
};

t_stat cpu_svc (UNIT *uptr)
{
    // poll kbd to sense ^E to halt cpu execution. 
    sim_activate_after (uptr, 300*1000);  // poll each 300 msec
    sim_poll_kbd();
    return SCPE_OK;
} 


// return 0 if drum addr invalid
int IsDrumAddrOk(int AR) 
{
    if ((AR < 0) || (AR >= (int)MEMSIZE) || (AR >= MAXMEMSIZE)) return 0;
    return 1; 
}

// return 0 if write addr invalid
int WriteDrum(int AR, t_int64 d, int NegZero)
{
    if (IsDrumAddrOk(AR) == 0) return 0;
    if (d) NegZero = 0; // sanity check on Minus Zero
    DRUM[AR] = d;
    DRUM_NegativeZeroFlag[AR] = NegZero;
    return 1;
}

// return 0 if drum addr invalid
int ReadDrum(int AR, t_int64 * d, int * NegZero)
{
    if (IsDrumAddrOk(AR) == 0) return 0;
    *d = DRUM[AR]; 
    *NegZero = DRUM_NegativeZeroFlag[AR];
    if (*d) {
        *NegZero = 0; // sanity check on Minus Zero
        DRUM_NegativeZeroFlag[AR] = 0;
    }
    return 1;
}

// return 0 if read addr invalid
int ReadAddr(int AR, t_int64 * d, int * NegZero)
{
    int r;
    int neg;

    if (AR == 8000) {*d = CSW;    neg=0;                    r=1; } else 
    if (AR == 8001) {*d = DIST;   neg=DistNegativeZeroFlag; r=1; } else
    if (AR == 8002) {*d = ACC[0]; neg=AccNegativeZeroFlag;  r=1; } else
    if (AR == 8003) {*d = ACC[1]; neg=AccNegativeZeroFlag;  r=1; } else 
    { r=ReadDrum(AR, d, &neg);                                   }
    if (*d) neg = 0; // sanity check on Minus Zero
    if (NegZero != NULL) *NegZero = neg;
    return r;
}

int bAccNegComplement; // flag to signals acc has complemented a negative ass (== sign adjust) 
                       // needed to compute execution cycles taken by the intruction

// add to accumulator, set Overflow
void AddToAcc(t_int64 a1, t_int64 a0) 
{
    OV = 0; AccNegativeZeroFlag = 0; 
    bAccNegComplement = 0;
    
    ACC[0] += a0;
    ACC[1] += a1;

    // adjust carry from Lower ACC to Upper Acc
    if (ACC[0] >=  D10) { ACC[0] -= D10; ACC[1]++; }
    if (ACC[0] <= -D10) { ACC[0] += D10; ACC[1]--; }

    // ajust sign
    if ((ACC[0] > 0) && (ACC[1] < 0)) {
        ACC[0] -= D10; ACC[1]++; 
        bAccNegComplement = 1;
    }
    if ((ACC[0] < 0) && (ACC[1] > 0)) {
        ACC[0] += D10; ACC[1]--; 
        bAccNegComplement = 1;
    }

    // check overflow
    if ((ACC[1] >= D10) || (ACC[1] <= -D10)) { 
        ACC[1] = ACC[1] % D10; 
        OV=1; 
    }
}

// shift acc 1 digit. If direction > 0 to the left, if direction < 0 to the right. 
// Return overflow digit (with sign)
int ShiftAcc(int direction)
{
    t_int64 a0, a1;
    int neg = 0;
    int n, m;

    n = 0;
    a1 = ACC[1]; if (a1 < 0) {a1 = -a1; neg = 1;}
    a0 = ACC[0]; if (a0 < 0) {a0 = -a0; neg = 1;}

    if (direction > 0) {                          // shift left
        n = Shift_Digits(&a1, 1);                 // n = Upper Acc high digit shifted out on the left
        m = Shift_Digits(&a0, 1);                 // m = intermediate digit that goes from one acc to the other
        a1 = a1 + (t_int64) m;
    } else if (direction < 0) {                   // shift right
        m = Shift_Digits(&a1, -1);                // m = intermediate digit that goes from one acc to the other
        n = Shift_Digits(&a0, -1);                // n = Lower Acc units digit shifted out on the right
        a0 = a0 + (t_int64) m * (1000000000L);     
    }
    if (neg) {a1=-a1; a0=-a0; n=-n;}

    ACC[0] = a0; 
    ACC[1] = a1; 
    if ((neg == 1) && (a0 == 0) && (a1 == 0)) AccNegativeZeroFlag = 1; 
    return n;
}


t_int64 SetDA(t_int64 d, int DA) 
{
    int neg = 0;

    int op, nn, IA;

    if (DA < 0) DA=-DA;
    if (d < 0) {d=-d; neg=1;}

    // extract parts of word
    op = Shift_Digits(&d, 2);          
    nn = Shift_Digits(&d, 4);          // discard current DA
    IA = Shift_Digits(&d, 4);         
    // rebuild word with new DA
    d = (t_int64) op * D8 + 
        (t_int64) DA * D4 + 
        (t_int64) IA;
    if (neg) d=-d;
    return d;
}

// set last 4 digits in d with IA contents
t_int64 SetIA(t_int64 d, int IA) 
{
    int neg = 0;
    
    if (IA < 0) IA=-IA;
    if (d < 0) {d=-d; neg=1;}
    d = d - ( d % D4);
    d = d + (IA % D4);
    if (neg) d=-d;
    return d;
}

// set last 2 digits in d with IA contents
t_int64 SetIA2(t_int64 d, int n) 
{
    int neg = 0;
    
    if (n < 0) n=-n;
    if (d < 0) {d=-d; neg=1;}
    d = d - ( d % 100);
    d = d + ( n % 100);
    if (neg) d=-d;
    return d;
}

// opcode decode 
// input: prior to call DecodeOpcode PR cpu register must be loaded with the word to decode 
// output: decoded instruction as opcode, DA, IA parts
//         bReadDrum: =1 if instruction needes to read data from drum before execution
//         returns opname: points to opcode name or NULL if undef opcode
CONST char * DecodeOpcode(int * opcode, int * DA, int * IA, int * bReadData)
{
    t_int64 d;
    CONST char * opname;

    d = PR;
    *opcode = Shift_Digits(&d, 2);          // current inste opcode
    *DA     = Shift_Digits(&d, 4);          // addr of data used by current instr
    *IA     = Shift_Digits(&d, 4);          // addr of next instr
    opname  = get_opcode_data(*opcode, bReadData);
    return opname;
}

// opcode execution 
// input: opcode, DA (data address), DrumAddr (current word under the r/w heads. Needed to calculate time used on instr execution)
//        prior to call ExecOpcode DIST cpu register must be loaded with the needed data for inst execution
// output: bWriteDrum: =1 if DIST must be written back to drum
//         bBranchToDA: =1 if next inst must be taken from DA register instead of DA
//         CpuStepsUsed: number of steps (=word time) used on program execution
t_stat ExecOpcode(int opcode, int DA, 
                  int * bWriteDrum, int * bBranchToDA, 
                  int DrumAddr, 
                  int * CpuStepsUsed)
{
    t_stat reason = 0;
    t_int64 d;
    int i, n, neg; 

    *bBranchToDA  = 0; 
    *bWriteDrum   = 0;
    *CpuStepsUsed = 0;

    switch(opcode) {
        case OP_NOOP   :   // No operation 
            if ((IC == 0) && ((PR % D4) == 0)) reason = STOP_HALT; // if loop on NOOP on addr zero -> machine idle -> stop cpu
            break;
        case OP_STOP   :   // Stop if console switch is set to stop, otherwise continue as a NO-OP 
            if (CSWProgStop) {
                reason = STOP_PROG;
                // normal stops has the consequence to prevent AR to be set with IA contents (to point to next instruction).
                // but STOP allows the user to resume execution with program start key on console (= scp go command)
                // so to allow this here we must explicitelly update AR
                AR = (PR % D4);        
            }
            break;
        // arithmetic
        case OP_RAL:   // Reset and Add into Lower
        case OP_RSL:   // Reset and Subtract into Lower
        case OP_RAABL: // Reset and Add Absolute into Lower
        case OP_RSABL: // Reset and Subtract Absolute into Lower
            d = DIST;
            if ((opcode == OP_RAABL) || (opcode == OP_RSABL)) d = AbsWord(d);
            if ((opcode == OP_RSL)   || (opcode == OP_RSABL)) d = -d;
            OV = 0; AccNegativeZeroFlag = 0;
            ACC[1] = 0;
            ACC[0] = d;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            // sequence chart for Add/Substract
            // (1)     (0..49)   (1)      (0/1)     (2)       (0/2)       (1)
            // Enable  Search    Data to  Wait      Dist to   Complement  Remove A
            // Dist    Data      dist     for even  Acc       Neg Sum     interlock
            //                                      (1)       (1)         (1)          (0..49)
            //                                      Restart   IA to AR    Enable PR    Search next
            //                                      Signal                             Inst
            *CpuStepsUsed = 1+1+2+1
                            +(DrumAddr % 2); // using lower acc -> wait for even 
            // no need to complement neg sum
            break;
        case OP_AL: // Add to Lower
        case OP_SL: // Subtract from Lower
        case OP_AABL: // Add Absolute to lower
        case OP_SABL: // Subtract Absolute from lower
            if ((opcode == OP_AL) && (ACC[1] == 0) && (ACC[0] == 0) && (AccNegativeZeroFlag) && 
                (DIST == 0) && (DistNegativeZeroFlag)) {
                // special case as stated in Operation manual 22(22-6060-2_650_OperMan.pdf), page 95
                // Acc result on minus zero if acc contains minus zero and AU or AL with a drum 
                // location that contains minus zero
                OV=0;
                sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: 0000000000 0000000000- (Minus Zero), OV: 0\n");
                // acc keeps the minus zero it already has
                break; 
            }
            d = DIST;
            if ((opcode == OP_AABL) || (opcode == OP_SABL)) d = AbsWord(d);
            if ((opcode == OP_SL)   || (opcode == OP_SABL)) d = -d;
            AddToAcc(0,d);
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            *CpuStepsUsed = 1+1+2+1
                            +(DrumAddr % 2)                 // using lower acc -> wait for even 
                            +(bAccNegComplement ? 2:0);     // acc sign change -> need to complement neg sum (two steps)
            break;
        case OP_RAU: // Reset and Add into Upper
        case OP_RSU: // Reset and Subtract into Upper
        case OP_AU:  // Add to Upper
        case OP_SU:  // Substract from Upper
            if ((opcode == OP_AU) && (ACC[1] == 0) && (ACC[0] == 0) && (AccNegativeZeroFlag) && 
                (DIST == 0) && (DistNegativeZeroFlag)) {
                // special case as stated in Operation manual 22(22-6060-2_650_OperMan.pdf), page 95
                // Acc result on minus zero if acc contains minus zero and AU or AL with a drum 
                // location that contains minus zero
                OV=0;
                sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: 0000000000 0000000000- (Minus Zero), OV: 0\n");
                // acc keeps the minus zero it already has
                break; 
            }
            d = DIST;
            if ((opcode == OP_RAU) || (opcode == OP_RSU)) ACC[1] = ACC[0] = 0;
            if ((opcode == OP_SU)  || (opcode == OP_RSU)) d = -d;
            AddToAcc(d,0);
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                 OV);
            *CpuStepsUsed = 1+1+2+1
                            +((DrumAddr+1) % 2)             // using upper acc -> wait for odd
                            +(bAccNegComplement ? 2:0);     // acc sign change -> need to complement neg sum (two steps)
            break;
        // Multiply/divide
        case OP_MULT: // Multiply
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Mult ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            sim_debug(DEBUG_DETAIL, &cpu_dev,   "...  by DIST: %06d%04d%c\n", 
                                                               printfd);
            if ((ACC[1] == 0) && (ACC[0] == 1) && (DIST == 0) && (DistNegativeZeroFlag)) {
                // special case as stated in Operation manual 22(22-6060-2_650_OperMan.pdf), page 95
                // Acc result on minus zero if a drum location that contains minus zero
                // is multiplied by +1
                OV = 0;
                sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: 0000000000 0000000000- (Minus Zero), OV: 0\n");
                // acc set to minus zero 
                ACC[1] = ACC[0] = 0;
                AccNegativeZeroFlag = 1;
                break; 
            }
            OV = 0;
            neg = (DIST < 0) ? 1:0; if (AccNegative) neg = 1-neg;
            d      = AbsWord(DIST); 
            ACC[0] = AbsWord(ACC[0]); 
            ACC[1] = AbsWord(ACC[1]); 
            for(i=0;i<10;i++) {
                n = ShiftAcc(1);
                while (n-- > 0) {
                    AddToAcc(0, d);
                    if (OV) break;
                }
                if (OV) break;
            }
            if (neg) {
                ACC[0] = -ACC[0]; 
                ACC[1] = -ACC[1]; 
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa, 
                OV);
            // sequence chart for Multiply/Divide
            // (1)     (0..49)   (1)      (0/1)     (20..200)  (1)
            // Enable  Search    Data to  Wait      Mult/Div   Remove A
            // Dist    Data      dist     for even  loop       interlock
            //                                      (1)       (1)         (1)          (0..49)
            //                                      Restart   IA to AR    Enable PR    Search next
            //                                      Signal                             Inst
            *CpuStepsUsed = 1+1+1+1
                            +(DrumAddr % 2)                 // wait for even 
                            +20*(i+1);                      // i holds the number of loops done
            break;
        case OP_DIV: // Divide
        case OP_DIVRU: // Divide and reset upper accumulator
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Div ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... by DIST: %06d%04d%c\n", 
                                                               printfd);
            if (DIST == 0) {
                OV = 1;
                sim_debug(DEBUG_DETAIL, &cpu_dev, "Divide By Zero -> OV set\n");
            } else if (AbsWord(DIST) <= AbsWord(ACC[1])) {
                OV = 1;
                sim_debug(DEBUG_DETAIL, &cpu_dev, "Quotient Overflow -> OV set and ERROR\n");
                reason = STOP_OV; // quotient overfow allways stops the machine
            } else {
                OV = 0;
                neg = (DIST < 0) ? 1:0; if (AccNegative) neg = 1-neg;
                d      = AbsWord(DIST); 
                ACC[0] = AbsWord(ACC[0]); 
                ACC[1] = AbsWord(ACC[1]); 
                for(i=0;i<10;i++) {
                    ShiftAcc(1);
                    while (d <= ACC[1]) {
                        AddToAcc(-d, 0);
                        ACC[0]++;
                    }
                }
                if (neg) {
                    ACC[0] = -ACC[0]; 
                    ACC[1] = -ACC[1]; 
                }
                if (opcode == OP_DIVRU) {
                    ACC[1] = 0;
                }
                *CpuStepsUsed = 1+1+1+1                    
                                +(DrumAddr % 2)                 // wait for even 
                                +20*(i+1) + 40;                 // i holds the number of loops done
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Div result ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            break;
        // shift
        case OP_SLT: // Shift Left
        case OP_SRT: // Shift Right
        case OP_SRD: // Shift Right and Round
            n = DA % 10;   // number of digits to shift
            d = 0;
            while (n-- > 0) {
                d = ShiftAcc((opcode == OP_SLT) ? 1:-1);
            }
            if (opcode == OP_SRD) {
                if (d <= - 5) AddToAcc(0,-1);
                if (d >=   5) AddToAcc(0,+1);
                OV = 0;
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            // sequence chart for shift
            // (1)       (0/1)      (2)      (1)
            // Enable    Wait       Per      Remove A
            // Sh count  for even   shift    interlock
            //                      (0/1)    (1)         (1)          (0..49)
            //                      Restart  IA to AR    Enable PR    Search next
            //                      Signal                            Inst
            *CpuStepsUsed = 1+1+1
                            +(DrumAddr % 2)                 // wait for even 
                            + 2*(DA % 10)                   // number of shifts done 
                            + ((opcode == OP_SRD) ? 1:0);
            break;
        case OP_SCT    :   // Shift accumulator left and count 
            n = 10 - DA % 10; // shift count (nine's complement of unit digit of DA)
            if (n==10) n=0;
            if (ACC[1] == 0) {  
                // upper acc is zero -> will have 10 or more shifts
                ACC[1] = ACC[0];
                ACC[0] = 10;
                if (n) {
                    OV = 1; // overflow because n <> 0 
                } else {
                    if (Get_HiDigit(ACC[1]) == 0) OV = 1; // overflow because not just 10 shifts
                }
            } else if (Get_HiDigit(ACC[1]) != 0) {  
                // no shift will be done
                ACC[0] = SetIA2(ACC[0], 0); // replace last two digits by 00
            } else {
                while (Get_HiDigit(ACC[1]) == 0)  {
                    ShiftAcc(1); // shift left
                    if (n==10) {
                        OV = 1;
                        break;
                    }
                    n++;
                }
                ACC[0] = SetIA2(ACC[0], n); // replace last two digits by 00
            }
            AccNegativeZeroFlag = 0;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            *CpuStepsUsed = 1+1+1
                            +(DrumAddr % 2)                 // wait for even 
                            + 2*(DA % 10);                  // number of shifts done 
            break;
        // load and store
        case OP_STL: // Store Lower in Mem
        case OP_STU: // Store Upper in Mem
            if ((ACC[0] == 0) && (ACC[1] == 0) && (AccNegativeZeroFlag)) {
                DistNegativeZeroFlag = 1;
            } else {
                DistNegativeZeroFlag = 0;
            }
            DIST = (opcode == OP_STU) ? ACC[1] : ACC[0];
            *bWriteDrum = 1; // to write DIST in drum at AR
            // sequence chart for store
            // (1)       (0/1)      (1)      (0..49)  (1)     (1)        (1)
            // Enable    Wait       L/U acc  Search   Store   IA to AR   Enable PR
            // Dist      for even   to dist  data     data
            //           or odd     
            *CpuStepsUsed = 1+1+1+1+1+                   
                            + (((opcode == OP_STU) ? DrumAddr:DrumAddr+1) % 2);   // wait for odd/even depending on STU/STL opcode
            break;
        case OP_STD: // store distributor
            *bWriteDrum = 1; // to write DIST in drum at AR
            *CpuStepsUsed = 1+1+1+1;                    
            break;
        case OP_STDA: // Store Lower Data Address
            n  = ((ACC[0] / D4) % D4);     // get data addr xxDDDDxxxx from lower Acc
            d = SetDA(DIST, n);            // replace it in distributor       
            if ((d == 0) && ((DIST < 0) || ( (DIST == 0) && (DistNegativeZeroFlag) ))) {
                // if dist results in zero but was negative or negative zero before replacing digits 
                // then it is set to minus zero 
                DistNegativeZeroFlag = 1; 
            } else {
                DistNegativeZeroFlag = 0;
            }
            DIST = d; 
            *bWriteDrum = 1; // to write DIST in drum at AR
            *CpuStepsUsed = 1+1+1+1
                            +(DrumAddr % 2);                 // wait for even 
            break;
        case OP_STIA: // Store Lower Instruction Address
            n = (ACC[0] % D4);            // get inst addr xxyyyyAAAA
            d = SetIA(DIST, n);           // replace it in distributor       
            if ((d == 0) && ((DIST < 0) || ( (DIST == 0) && (DistNegativeZeroFlag) ))) {
                // if dist results in zero but was negative or negative zero before replacing digits 
                // then it is set to minus zero 
                DistNegativeZeroFlag = 1; 
            } else {
                DistNegativeZeroFlag = 0;
            } 
            DIST = d; 
            *bWriteDrum = 1; // to write DIST in drum at AR
            *CpuStepsUsed = 1+1+1+1
                            +(DrumAddr % 2);                 // wait for even 
            break;
        case OP_LD:  // Load Distributor
            *CpuStepsUsed = 1+1+1+1;                    
            break;
        case OP_TLU    :   // Table lookup 
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Search DIST: %06d%04d%c\n", 
                                                               printfd);

            AR = (DA / 50) * 50; // set AR to start of band based on DA
            AR--; n=-1;
            while (1) {
                AR++; n++;
                if (0==IsDrumAddrOk(AR)) {
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "Invalid AR addr %d ERROR\n", AR);
                    reason = STOP_ADDR;
                    break;
                }
                if ((AR % 50) > 47) continue; // skip addr 48 & 49 of band that cannot be used for tables
                if (0==ReadAddr(AR, &d, NULL)) {    // read table argument
                    reason = STOP_ADDR; 
                    break;
                }
                if (AbsWord(d) >= AbsWord(DIST)) break; // found
            }
            // set the result as xxNNNNxxxx in lower acc
            ACC[0] = SetDA(ACC[0], DA+n);                 
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Result ACC: %06d%04d %06d%04d%c, OV: %d\n",
                printfa,
                OV);
            *CpuStepsUsed = 1+1+1+1+1+1
                            +(DrumAddr % 2)                 // wait for even 
                            + n;                            // number of reads to find the argument searched for
            break;
        // branch
        case OP_BRD1: case OP_BRD2: case OP_BRD3: case OP_BRD4: case OP_BRD5: // Branch on 8 in distributor positions 1-10 
        case OP_BRD6: case OP_BRD7: case OP_BRD8: case OP_BRD9: case OP_BRD10:
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Check DIST: %06d%04d%c\n", 
                                                              printfd);
            d = DIST; 
            n = opcode - OP_BRD10; if (n == 0) n = 10;
            while (--n > 0) d = d / 10;
            d = d % 10;
            if (d == 8) {
               sim_debug(DEBUG_DETAIL, &cpu_dev, "Digit is %d -> Branch Taken\n", (int32) d);
               *bBranchToDA = 1; // IA (next instr addr) will be taken from DA. Branch taken
            } else if (d == 9) {
               // IA kept as already set. Branch not taken
               sim_debug(DEBUG_DETAIL, &cpu_dev, "Digit is %d -> Branch Not Taken\n", (int32) d);
            } else {
               // any other value for tested digit -> stop
               sim_debug(DEBUG_DETAIL, &cpu_dev, "Digit is %d -> Branch ERROR\n", (int32) d);
               reason = STOP_ERRO;
               break;
            }
            *CpuStepsUsed = 1+1                    
                            + ((*bBranchToDA) ? 1:0);   // one extra step needed if branch taken
            break;
        case OP_BRNZU: // Branch on Non-Zero in Upper
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            if (ACC[1] != 0) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "Upper ACC not Zero -> Branch Taken\n");
                *bBranchToDA = 1;
            }
            *CpuStepsUsed = 1+1                    
                            +(DrumAddr % 2)             // wait for even 
                            + ((*bBranchToDA) ? 1:0);   // one extra step needed if branch taken
            break;
        case OP_BRNZ: // Branch on Non-Zero 
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            if ((ACC[1] != 0) || (ACC[0] != 0)) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "ACC not Zero -> Branch Taken\n");
                *bBranchToDA = 1; 
            }
            *CpuStepsUsed = 1                    
                            +((DrumAddr+1) % 2)         // wait for odd
                            + ((*bBranchToDA) ? 1:0);   // one extra step needed if branch taken
            break;
        case OP_BRMIN: // Branch on Minus
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            if (AccNegative) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "ACC is Negative -> Branch Taken\n");
                *bBranchToDA = 1; 
            }
            *CpuStepsUsed = 1+1                    
                            + ((*bBranchToDA) ? 1:0);   // one extra step needed if branch taken
            break;
        case OP_BROV: // Branch on Overflow
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Check OV: %d\n", OV);
            if (OV) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "OV Set -> Branch Taken\n");
                *bBranchToDA = 1; 
            }
            *CpuStepsUsed = 1+1                    
                            + ((*bBranchToDA) ? 1:0);   // one extra step needed if branch taken
            break;
        // Card I/O
        case OP_RD     :   // Read a card 
            AR = (DA / 50) * 50 + 1; // Read Band is XX01 to XX10 or XX51 to XX60
            {
                uint32             r;
                int i; 
                char s[6];

                r = cdr_cmd(&cdr_unit[1], IO_RDS,AR);
                if (r == SCPE_NOCARDS) {
                    reason = STOP_CARD;
                    break;
                }
                for (i=0;i<10;i++) {
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "... Read Card %04d: %06d%04d%c '%s'\n", 
                        AR+i, printfw(DRUM[AR+i],DRUM_NegativeZeroFlag[AR+i]), 
                        word_to_ascii(s, 1, 5, DRUM[AR+i]));
                }
                if (cdr_unit[1].u5 & URCSTA_LOAD) {
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "... Is a LOAD Card\n");
                    *bBranchToDA = 1;      // load card -> next inste is taken from DA
                }
            }
            // 300 msec read cycle, 270 available for computing
            *CpuStepsUsed = 312; // 30 msec / 0.096 msec word time;                    
            break;
        case OP_PCH    :   // Punch a card 
            AR = (DA / 50) * 50 + 27; // Read Band is XX27 to XX36 or XX77 to XX86
            {
                uint32             r;
                int i; 
                char s[6];

                for (i=0;i<10;i++) {
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "... Punch Card %04d: %06d%04d%c '%s'\n", 
                        AR+i, printfw(DRUM[AR+i],DRUM_NegativeZeroFlag[AR+i]), 
                        word_to_ascii(s, 1, 5, DRUM[AR+i]));
                }
                r = cdp_cmd(&cdp_unit[1], IO_WRS,AR);
                if (r == SCPE_NOCARDS) {
                    reason = STOP_CARD;
                    break;
                }
            }
            // 600 msec punch cycle, 565 available for computing
            *CpuStepsUsed = 365; // 35 msec / 0.096 msec word time;                    
            break;
        default:
            reason = STOP_UUO;
            break;
    }
    if ((reason == 0) && (OV) && (CSWOverflowStop)) reason = STOP_OV;

    return reason;
}

t_stat
sim_instr(void)
{
    t_stat              reason;
    int                 opcode, halt_cpu;
    int                 bReadData, bWriteDrum, bBranchToDA;
    int                 instr_count = 0; /* Number of instructions to execute */
    const char *        opname;          /* points to opcode name */               

    int IA = 0;                                     // Instr Address: addr of next inst 
    int DA = 0;                                     // Data Address; addr of data to be used by current inst

    int DrumAddr;                                   // address where drum is currently positioned (0-49)
    int MachineCycle, CpuStepsUsed, WaitForInterlock;

    #define IL_RD1   1                              // interlock on drum area 01-10/51-60 used in reading with RD1
    #define IL_WR1   2                              // interlock on drum area 27-36/77-86 used in writing for WR1
    int InterLockCount[3];                          // interlock counters

    /* How CPU execution is simulated

    A cpu instruction is executed in real hw in several steps. Some os these steps involves waiting for rotating 
    drum to be positioned on requested addres (register AR). Other steps can involve waiting a Interlock to be released.
    The execution of a complete instruction is called a machine cycle

    User can select in real hw control panel to execute the instructions one by one. The execution is not done
    on full instruction (a full cycle), but rather in instruction half-cycles: I-Cycle and D-Cycle.
    During I-Cycle, the instruction is fetched from drum and decoded. During D-Cycle instruction is performed.

    The simulator models this using the concept of MachineCycles, that groups several steps on opcode execution

    SimH             Real hw equivalent
    machine cycle    half cycle
    0                I-Cycle        wait for drum to be positioned at address given by AR cpu register 
    1                I-Cycle        read the drum to PR register, 
                                    decode as opcode, DA, IA, 
                                    check if must wait for interlock
                                    if decoded opcode reads data from drum set AR=DA
    2                D-Cycle        wait for interlock if needed
                                    wait for drum to be positioned at AR address if decoded opcode reads data from drum
    3                D-Cycle        if decoded opcode reads data from drum, read the data in DIST
                                    set interlock if needed
                                    perform opcode operation
    4                D-Cycle        wait opcode excution time 
                                    wait for drum to be positioned at AR addressif executed opcode writes data to drum
    5                D-Cycle        if executed opcode writes data to drum, write DIST to drum 
                                    set AR=IA to read next instruction 

    */

    if (sim_step != 0) {
        instr_count = sim_step;
        sim_cancel_step();
    }

    reason = halt_cpu = 0;

    MachineCycle = CpuStepsUsed = 0;
    DrumAddr = 0;

    WaitForInterlock = 0;
    InterLockCount[IL_RD1] = InterLockCount[IL_WR1] = 0;

    sim_cancel (&cpu_unit);
    sim_activate (&cpu_unit, 1);     

    while (reason == 0) {       /* loop until halted */

        if (sim_interval <= 0) {        /* event queue? */
            reason = sim_process_event();
            if (reason == SCPE_STOP) {
                reason = 0;    // if stop cpu requested, does not do it inmediatelly
                halt_cpu = 1;  // signal it so cpu is halted on end of current intr
            }
            if (reason != SCPE_OK) {
                break;      /* process */
            }
        }

        /* Main instruction fetch/decode loop */
        sim_interval -= 1;         /* count down */

        // simulate the rotating drum: incr current drum position
        DrumAddr = (DrumAddr+1) % 50;
        // if any interlock set, decrease it 
        if (InterLockCount[IL_RD1]) InterLockCount[IL_RD1]--;
        if (InterLockCount[IL_WR1]) InterLockCount[IL_WR1]--;


        // simulates the machine working on half cycles
        if ((HalfCycle == 1) && (MachineCycle == 2)) {      // if I-Half finished, about to exec D-Half
           HalfCycle = 2;                                   // bump half cycle to exec D-Half on next scp step
           reason = SCPE_STEP;                              // then break beacuse I-Half finished
           break; 
        } 
        if ((HalfCycle == 2) && (MachineCycle == 0)) {      // if D-Half should start
           HalfCycle = 1;                                   // bump half cycle to exec I-Half on next scp step
           instr_count = 1;                                 // break at the end of D-half execution
           MachineCycle = 3;
        }

        if (MachineCycle == 0) {
            /* Only check for break points during actual fetch */
            if (sim_brk_summ && sim_brk_test(IC, SWMASK('E'))) {
                reason = STOP_IBKPT;
                break;
            }
            // only check for ^E on fetch
            if (halt_cpu) {
                reason = SCPE_STOP; 
                break;
            }
            // should wait for drum to fetch inst?
            if (AR < (int)MEMSIZE) {
                if ((AR % 50) != DrumAddr) continue; // yes
            }
            CpuStepsUsed = 0; // init inst execution
            MachineCycle = 1; // decode instr
        } if (MachineCycle == 2) {
            // should wait for cpu to exec the inst?
            if (CpuStepsUsed > 0) {CpuStepsUsed--; continue;} // yes
            // should wait for interlock release?
            if (WaitForInterlock) {
                if (InterLockCount[WaitForInterlock]) continue; // yes
                WaitForInterlock = 0;
            }
            // should wait for drum to fetch data?
            if ((bReadData) && (AR >= 0) && (AR < (int)MEMSIZE)) {
                if ((AR % 50) != DrumAddr) continue; // yes
            }
            MachineCycle = 3; // exec instr
        } if (MachineCycle == 4) {
            // should wait for cpu to exec the inst?
            if (CpuStepsUsed > 0) {CpuStepsUsed--; continue;} // yes
            // should wait for drum to store data?
            if ((bWriteDrum) && (AR >= 0) && (AR < (int)MEMSIZE)) {
                if ((AR % 50) != DrumAddr) continue; // yes
            }
            MachineCycle = 5; // terminate the instr execution
        }
        // here, MachineCycle is either 1 (decode), 3 (exec-read), 5 (exec-write)
        
        if (MachineCycle == 1) {
            // fetch current intruction from mem, save current instr addr in IC
            IC = AR;
            if (0==ReadAddr(AR, &PR, NULL)) {
                reason = STOP_ADDR; 
                goto end_of_cycle;
            }
            // decode inst
            opname = DecodeOpcode(&opcode, &DA, &IA, &bReadData);
            sim_debug(DEBUG_CMD, &cpu_dev, "Exec %04d: %02d %-6s %04d %04d %s%s\n", 
                                           IC, opcode, (opname == NULL) ? "???":opname, DA, IA,
                                           (DRUM_Symbolic_Buffer[AR * 80] == 0) ? "" : "            symb: ", 
                                           &DRUM_Symbolic_Buffer[AR * 80]);
            if (opname == NULL) {
                reason = STOP_UUO; 
                goto end_of_cycle;
            }

            AR = DA;    // allways trasnfer DA to AR even if drum will be not read. This is why 
                        // all opcodes must have a valid DA address even if not used to read drum (eg SRT 0003 to shift)

            // check if opcode should wait for and already set interlock
            if ((opcode == OP_RD) && (InterLockCount[IL_RD1])) {
                WaitForInterlock = IL_RD1;
            } else if ((opcode == OP_PCH) && (InterLockCount[IL_WR1])) {
                WaitForInterlock = IL_WR1;
            } else {
                WaitForInterlock = 0;
            }

            MachineCycle = 2;
            continue;
        }

       
        if (MachineCycle == 3) {
            // decode again PR register to reload internal register DA, IA, AR again. Needed if we are executing half cycles  
            opname = DecodeOpcode(&opcode, &DA, &IA, &bReadData);
            AR = DA;
            if (opname == NULL) {
                reason = STOP_UUO; 
                goto end_of_cycle;
            }
            // fetch data from drum if needed
            if (bReadData) {
                if (0==ReadAddr(AR, &DIST, &DistNegativeZeroFlag)) {
                    sim_debug(DEBUG_DATA, &cpu_dev, "... Read %04d: ???\n", 
                                                           AR);
                    reason = STOP_ADDR; 
                    goto end_of_cycle;
                } else {
                    sim_debug(DEBUG_DATA, &cpu_dev, "... Read %04d: %06d%04d%c\n", 
                                                           AR,   printfd);
                }
            } else {
                if (0==IsDrumAddrOk(AR)) {  // even if no data is fetched from drum, DA addr must be a valid one
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "Invalid AR addr %d ERROR\n", AR);
                    reason = STOP_ADDR;
                    goto end_of_cycle;
                }
            }

            // check if opcode should set interlock
            if (opcode == OP_RD) {
                InterLockCount[IL_RD1] = 3120; // 300 msec for read card processing
            } else if ((opcode == OP_PCH) && (InterLockCount[IL_WR1])) {
                InterLockCount[IL_WR1] = 6250; // 600 msec for punch card processing
            }

            reason = ExecOpcode(opcode, DA, 
                                &bWriteDrum, &bBranchToDA, 
                                DrumAddr, &CpuStepsUsed);
            if (reason != 0) goto end_of_cycle;

            if (bBranchToDA) IA = DA; 

            MachineCycle = 4;
            if (CpuStepsUsed > 2) CpuStepsUsed -= 2; // decrease by 2 as each inst passes at minimum two times by DrumAddr incr
            continue; 
        }

        if (MachineCycle == 5) {
            if (bWriteDrum) {
                sim_debug(DEBUG_DATA, &cpu_dev, "... Write %04d: %06d%04d%c\n",  
                                                            AR,   printfd);
                if (0==WriteDrum(AR, DIST, DistNegativeZeroFlag)) {
                    reason = STOP_ADDR; 
                    goto end_of_cycle;
                }
            }
            // set AR to point to next instr
            AR = IA;        
            // do not continue, just go on end_of_cycle
        }
end_of_cycle:

        if (instr_count != 0 && --instr_count == 0) {
            if (reason == 0) {
                IC = AR; 
                // if cpu not stoped (just stepped) set IC so next inst to be executed is shown. 
                // if cpu stopped because some error (reason != 0), does not advance IC so instr shown is offending one
                reason = SCPE_STEP; 
                break;
            }
        }
        MachineCycle = 0; // ready to process to next instr

    } /* end while */

    // flush 407 printout 
    if ((cdp_unit[0].flags & UNIT_ATT) && (cdp_unit[0].fileref)) {
        fflush(cdp_unit[0].fileref);
    }

    /* Simulation halted */

    return reason;
}


/* Reset routine */
t_stat
cpu_reset(DEVICE * dptr)
{

    ACC[0] = ACC[1] = DIST = 0;
    PR = AR = OV = 0;
    AccNegativeZeroFlag = 0;
    DistNegativeZeroFlag = 0;
    IC = 0;

    sim_brk_types = sim_brk_dflt = SWMASK('E');
    return SCPE_OK;
}

/* Memory examine */

t_stat
cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw)
{
    t_int64  d;
    int NegZero; 
    t_value val;

    if (0==ReadAddr(addr, &d, &NegZero)) {
        return SCPE_NXM;
    }
    if (vptr != NULL) {
        if (NegZero) {
            val = NEGZERO_value; // val has this special value to represent -0 (minus zero == negative zero) 
        } else {
            val = (t_value) d;
        }
        *vptr = val;
    }

    return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw)
{
    t_int64 d; 
    if (addr >= MEMSIZE)
        return SCPE_NXM;
    d = val;
    if (val == NEGZERO_value) {
        DRUM[addr] = 0;     // Minus Zero is coded as val = NEGZERO_value constant
        DRUM_NegativeZeroFlag[addr] = 1;
    } else {
        DRUM[addr] = d;
        DRUM_NegativeZeroFlag[addr] = 0;
    }
    return SCPE_OK;
}

t_stat
cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 mc = 0;
    uint32              i;
    int32               v;

    v = val >> UNIT_V_MSIZE;
    if (v == 0) {v = 1000;} else 
    if (v == 1) {v = 2000;} else 
    if (v == 2) {v = 4000;} else v = 0;
    if ((v <= 0) || (v > MAXMEMSIZE))
        return SCPE_ARG;
    if (v < 4000) {
        for (i = v; i < MEMSIZE; i++) {
            if ((DRUM[i] != 0) || (DRUM_NegativeZeroFlag[i] != 0)) {
               mc = 1;
            break;
            }
        }
    }
    for (i=0;i<MAXMEMSIZE * 80;i++) 
        DRUM_Symbolic_Buffer[i] = 0;                // clear drum symbolic info
    if ((mc != 0) && (!get_yn("Really truncate memory [N]? ", FALSE)))
        return SCPE_OK;
    cpu_unit.flags &= ~UNIT_MSIZE;
    cpu_unit.flags |= val;
    MEMSIZE = v;
    for (i = MEMSIZE; i < MAXMEMSIZE; i++)
        DRUM[i] = DRUM_NegativeZeroFlag[i] = 0;
    return SCPE_OK;
}


t_stat
cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr) {
    fprintf (st, "These switches are recognized when examining or depositing in CPU memory:\r\n\r\n");
    fprintf (st, "      -c      examine/deposit characters, 5 per word\r\n");
    fprintf (st, "      -m      examine/deposit IBM 650 instructions\r\n\r\n");
    fprintf (st, "The memory of the CPU can be set to 1000, 2000 or 4000 words.\r\n\r\n");
    fprintf (st, "   sim> SET CPU nK\r\n\r\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char * cpu_description (DEVICE *dptr) {
    return "IBM 650 CPU";
}

