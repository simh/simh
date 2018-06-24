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

   IBM 653 Storage Unit can be enabled as an option. This simulates the following 
     - Immediate Access Storage (IAS)
     - Index registers 
     - Floating Point support
     - Synchronizers 2 & 3


*/

#include "i650_defs.h"

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (7 << UNIT_V_MSIZE)
#define UNIT_V_CPUMODEL (UNIT_V_UF + 4)
#define UNIT_MODEL      (0x01 << UNIT_V_CPUMODEL)
#define CPU_MODEL       ((cpu_unit.flags >> UNIT_V_CPUMODEL) & 0x01)
#define MODEL(x)        (x << UNIT_V_CPUMODEL)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
#define OPTION_STOR     (1 << (UNIT_V_CPUMODEL + 1))
#define OPTION_CNTRL    (1 << (UNIT_V_CPUMODEL + 2))
#define OPTION_SOAPMNE  (1 << (UNIT_V_CPUMODEL + 3))
#define OPTION_FAST     (1 << (UNIT_V_CPUMODEL + 4))

t_stat              cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw);
t_stat              cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw);
t_stat              cpu_reset(DEVICE * dptr);
t_stat              cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat              cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat              cpu_svc (UNIT *uptr);
const char          *cpu_description (DEVICE *dptr);

// DRUM Memory
t_int64             DRUM[MAXDRUMSIZE]                        = {0};
int                 DRUM_NegativeZeroFlag[MAXDRUMSIZE]       = {0};
char                DRUM_Symbolic_Buffer[MAXDRUMSIZE * 80]   = {0}; // does not exists on real hw. Used to keep symbolic info 

// IO Synchronizer for card read-punch buffer
t_int64             IOSync[10]                               = {0};
int                 IOSync_NegativeZeroFlag[10]              = {0};

// IAS Memory
t_int64             IAS[60]                                  = {0};
int                 IAS_NegativeZeroFlag[60]                 = {0};
int                 IAS_TimingRing                           = 0;

// interlock counters
int InterLockCount[IL_array]                                 = {0};                   

// address where rotating drum is currently positioned (0-49)
int DrumAddr;                                   

// cpu registers
uint16              IC;                          // Added register not part of cpu. Has addr of current intr in execution, just for displaying purposes. IBM 650 has no program counter
uint16              PROP;                        // Added register not part of cpu. Has operation code of current intr in execution, just for scp scripting purposes. Contains the two higher digits of PR register
t_int64             ACC[2];                      /* lower, upper accumulator. 10 digits (=one word) each*/
t_int64             DIST;                        /* ditributor. 10 digits */
t_int64             CSW = 0;                     /* Console Switches, 10 digits */
t_int64             PR;                          /* Program Register: hold current instr in execution, 10 digits*/
uint16              AR;                          /* Address Register: address references to drum */
uint8               OV;                          /* Overflow flag */
uint8               CSWProgStop     = 1;         /* Console programmed stop switch */
uint8               CSWOverflowStop = 0;         /* Console stop on overflow switch */
uint8 HalfCycle          = 0;                    // set to 0 for normal run, =1 to execute I-Half-cycle, =2 to execute D-half-cycle
int ProgStopFlag         = 0;                    // set to 1 if programmed stop was the previous inst executed
int AccNegativeZeroFlag  = 0;                    // set to 1 if acc has a negative zero
int DistNegativeZeroFlag = 0;                    // set to 1 if distributor has a negative zero
int16               IR[3];                       // Index registers. Are 4 digits as AR register, but signed


/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit =
    { UDATA(&cpu_svc, MEMAMOUNT(0)|MODEL(0x0), 1000), 10  };


REG                 cpu_reg[] = {
    {DRDATAD(IC, IC, 16, "Current Instruction"), REG_FIT|REG_RO},
    {DRDATAD(PROP, PROP, 16, "Program Register Operation Code"), REG_FIT|REG_RO},
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
    {UNIT_MSIZE,     MEMAMOUNT(0),    "1K", "1K", &cpu_set_size},
    {UNIT_MSIZE,     MEMAMOUNT(1),    "2K", "2K", &cpu_set_size},
    {UNIT_MSIZE,     MEMAMOUNT(2),    "4K", "4K", &cpu_set_size},
    {OPTION_STOR,    0,               NULL,                   "NOSTORAGEUNIT", NULL},
    {OPTION_STOR,    OPTION_STOR,     "Storage Unit",         "STORAGEUNIT",   NULL},
    {OPTION_CNTRL,   0,               NULL,                   "NOCNTRLUNIT",   NULL},   
    {OPTION_CNTRL,   OPTION_CNTRL,    "Control Unit",         "CNTRLUNIT",     NULL},
    {OPTION_SOAPMNE, 0,               NULL,                   "DEFAULTMNE",    NULL},
    {OPTION_SOAPMNE, OPTION_SOAPMNE,  "Using SOAP Mnemonics", "SOAPMNE",       NULL},
    {OPTION_FAST,    0,               NULL,                   "REALTIME",      NULL},
    {OPTION_FAST,    OPTION_FAST,     "Fast Execution",       "FAST",          NULL},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 10, 16, 1, 10, 64,
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


// return 0 if addr invalid, 1 if addr valid depending on allowed addrs given by ValidDA
// set the ias TimingRing to AR is IAS is accessed
int IsDrumAddrOk(int AR, int ValidDA) 
{
    // check if AR should be 9000 
    if ((STOR) && (ValidDA & vda_9000)) 
        return (AR == 9000) ? 1:0;
    // Drum address
    if ((AR >= 0) && (AR < DRUMSIZE)) 
        return (ValidDA & vda_D) ? 1:0; 
    // cpu registers: acc (lo&hi), distibutor, console swithc reg: ok to check for Addr validity, ok to read, cannot write to it
    if ((AR >= 8000) && (AR <= 8003)) 
        return (ValidDA & vda_A) ? 1:0; 
    // index registers (ir) present if Storage Unit is enabled: ok to check for Addr validity, ok to read, cannot write to it
    if ((STOR) && (AR >= 8005) && (AR <= 8007))
        return (ValidDA & vda_I) ? 1:0; 
    // tape address present is tapes are enabled: ok to check for Addr validity, cannot read/write to it
    if ((CNTRL) && (AR >= 8010) && (AR <= 8015))
        return (ValidDA & vda_T) ? 1:0; 
    // inmediate access storage (ias) if Storage Unit is enabled: ok to check for Addr validity, ok to read/write
    if ((STOR) && (AR >= 9000) && (AR <= 9059)) {   
        if (ValidDA & vda_S) {
            IAS_TimingRing = AR - 9000;                 // set Timing ring on address accesed
            return 1;
        }
    }
    // none of the above -> invalid address or address/mode combination
    return 0;
}

// return 0 if write addr invalid
int WriteAddr(int AR, t_int64 d, int NegZero)
{
    if (d) NegZero = 0; // sanity check on Minus Zero
    if ((STOR) && (AR >= 9000) && (AR <= 9059)) {   // IAS is available at addr 9000-9059
        IAS_TimingRing = AR - 9000;                 // not necessary as before any call to WriteAddr IsAddrOk is invoked. But ... just in case
        IAS[IAS_TimingRing] = d;
        IAS_NegativeZeroFlag[IAS_TimingRing] = NegZero;
        return 1;
    } else if ((AR >= 0) && (AR < DRUMSIZE) && (AR < MAXDRUMSIZE)) {
        if (d) NegZero = 0; // sanity check on Minus Zero
        DRUM[AR] = d;
        DRUM_NegativeZeroFlag[AR] = NegZero;
        return 1;
    }
    // none of the above -> invalid address or address/mode combination
    return 0;
}

// return 0 if read addr invalid
int ReadAddr(int AR, t_int64 * d, int * NegZero)
{
    int neg;

    // read from drum?
    if ((AR >= 0) && (AR < DRUMSIZE)) {
        *d  = DRUM[AR]; 
        neg = DRUM_NegativeZeroFlag[AR];
        if (*d) DRUM_NegativeZeroFlag[AR] = 0;
    } else
    // read from cpu registers?
    if (AR == 8000) {*d = CSW;    neg=0;                    } else 
    if (AR == 8001) {*d = DIST;   neg=DistNegativeZeroFlag; } else
    if (AR == 8002) {*d = ACC[0]; neg=AccNegativeZeroFlag;  } else
    if (AR == 8003) {*d = ACC[1]; neg=AccNegativeZeroFlag;  } else 
    // read index registers (ir) ? 
    if ((STOR) && (AR == 8005)) {*d = IR[0]; neg=0;         } else
    if ((STOR) && (AR == 8006)) {*d = IR[1]; neg=0;         } else
    if ((STOR) && (AR == 8007)) {*d = IR[2]; neg=0;         } else
    // tape address ? 
    if ((CNTRL) && (AR >= 8010) && (AR <= 8015)) {   
        // cannot read/write to tape addresses
        return 0;
    } else 
    // read inmediate access storage (ias)?
    if ((STOR) && (AR >= 9000) && (AR <= 9059)) {   
        IAS_TimingRing = AR - 9000; 
        *d  = IAS[IAS_TimingRing];
        neg = IAS_NegativeZeroFlag[IAS_TimingRing];
        if (*d) IAS_NegativeZeroFlag[IAS_TimingRing] = 0;
    } else {
        // none of the above -> invalid address for read
        return 0;
    }
    if (*d) neg = 0; // sanity check on Minus Zero
    if (NegZero != NULL) *NegZero = neg;
    return 1;
}

// shift acc 1 digit. If direction > 0 to the left, if direction < 0 to the right. 
// Return digit going out of acc (with sign)
int ShiftAcc(int direction)
{
    t_int64 a0, a1;
    int neg = 0;
    int n, m;

    n = 0;
    a1 = ACC[1]; if (a1 < 0) {a1 = -a1; neg = 1;}
    a0 = ACC[0]; if (a0 < 0) {a0 = -a0; neg = 1;}
    if ((AccNegativeZeroFlag) && (ACC[0] == 0) && (ACC[1] == 0)) neg = 1;

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


// float value format = mmmmmmmcc = 0.m x 10^(c-50)
//                      mmmmmmm = mantissa
//                      cc      = modified characteristic (== exponent)
// get modified characteristic of float d 
int GetExp(t_int64 d)
{
    return (AbsWord(d) % 100);
}

// set modified characteristic of float d 
t_int64 SetExp(t_int64 d, int exp)
{
    int neg = 0;

    if (d < 0) {neg=1; d=-d;}
    d = ((d / 100) * 100) + (exp % 100);
    if (neg) d=-d;
    return d;
}

// set result into ACC[1] and ACC[0] for float mult and division
// get a 10 digits mantissa en ACC[1] 
// round to the 8th digit
// add the modified characteristic (exp)
// add sign, check for zero
void MantissaRoundAndNormalizeToFloat(int * CpuStepsUsed, int neg, int exp)
{
    // if high order digit of mantissa is zero, shift left once
    if (Get_HiDigit(ACC[1]) == 0) {       
        ShiftAcc(1);
        *CpuStepsUsed = *CpuStepsUsed + 2;
        if (exp == 0) {
            OV = 1; 
        } else {
            exp--;
        }
    }
    // round mantissa in ACC[1] to the 8th digit
    if (GetExp(ACC[1]) >= 50) {
        ACC[1] = ACC[1] + 100;
        if (ACC[1] >= D10) {
            ACC[1] = ACC[1] / 10;
            *CpuStepsUsed = *CpuStepsUsed + 2;
            if (exp == 99) {
                OV = 1; 
            } else {
                exp++;
            }
        }
    }
    ACC[1] = SetExp(ACC[1], 0); 
    // normalize mantissas
    while (( ACC[1] != 0) && (Get_HiDigit(ACC[1]) == 0)) {       
        if (exp == 0) {                       
            OV = 1; 
            break;                  // if zero, underflow
        } else {
            exp--;
        }
        ACC[1] = ACC[1] * 10;
        *CpuStepsUsed = *CpuStepsUsed + 2;
    }
    // set result
    if (exp <  0) {exp += 100; OV = 1;}
    if (exp > 99) {exp -= 100; OV = 1;}
    ACC[1] = neg * SetExp(ACC[1], exp);
    ACC[0] = 0;
    if ((ACC[1] / 100) == 0) ACC[1] = 0; // if mantissa is zero, all is zero
    AccNegativeZeroFlag = 0;
}


// add float to accumulator, set Overflow
// return number of steps used
int AddFloatToAcc(int bSubstractFlag, int bAbsFlag, int bNormalizeFlag)
{
    int nSteps;
    int n, neg;
    t_int64 d;

    OV = 0; AccNegativeZeroFlag = 0; 
    nSteps = 0;

    n = GetExp(ACC[1]) - GetExp(DIST);
    if (n == 0) {
        // no decimal aligning necessary. Mantissas ready to being added
    } else if ( n > 8) {
        DIST = ACC[1]; ACC[1] = 0;
    } else if ( n < -8) {
        ACC[1] = 0;
    } else {
        if (n < 0) {                            // if between -1 and -8
            n = -n;                             // just remove sign on number of shifts to be done 
        } else {                                // if between 1 and 8
            d = ACC[1]; 
            ACC[1] = DIST; DIST = d;            // exchange distrib and upper acc
            nSteps += 2;
        }
        ACC[1] = SetExp(ACC[1], 0);             // modified characteristic of upper set to zero
        while (n>0) {                           // shift right n digits
            ShiftAcc(-1);
            nSteps += 2;
            n--;
        }
        if (GetExp(ACC[1]) >= 50) {             // should round?
            ACC[1] = ACC[1] + ((ACC[1] >= 0) ? 100:-100);
        }
    }
    d = DIST;
    if (bAbsFlag) if (d < 0) d = -d;
    if (bSubstractFlag) d = -d;

    if (((ACC[1] > 0) && (d < 0)) || ((ACC[1] < 0) && (d > 0))) nSteps += 4; 

    ACC[1] = (ACC[1] / 100) + (d / 100);        // add/substract mantissas (positions 10-3) 
    n = GetExp(DIST);                           // get  modified characteristic from dist
    if (ACC[1] < 0) {
        ACC[1] = -ACC[1]; neg=-1;
    } else {
        neg=1;
    }

    if (ACC[1] >= D8) {                         // overflow?
        if ((ACC[1] % 10) >= 5) {               // should round?
            ACC[1] = ACC[1] / 10 + 1;           // yes, shift right, keep extra 1 in high pos, add rounding
            nSteps += 4; 
        } else {
            ACC[1] = ACC[1] / 10;               // no, just shift
        }
        n++;                                    // add 1 to dist modified characteristic 
        if (n > 99) {OV = 1; n=0;}              // overflow. Set modified characteristic to zero
        nSteps += 4;
    }

    if (ACC[1] == 0) {
        n = 0;                                  // if mantissa is zero, mod. char is set to zero also
        bNormalizeFlag = 0;                     // must not normalize
        nSteps += 2;
    }
    ACC[1] = SetExp(neg * ACC[1] * 100, n);     // insert modified characteristic of dist into upper acc       
    ACC[0] = 0;                                 // lower acc set to zero
    if (bNormalizeFlag) {
        while(Get_HiDigit(ACC[1]) == 0) {       // while hi digit (digit 10) is zero -> normalize 
            n = GetExp(ACC[1]);                 // get modified characteristic
            if (n == 0) {                       
                OV = 1; break;                  // if zero, underflow
            }
            n--;
            ACC[1] = SetExp(ACC[1]/100 * 1000, n);  // left shift mantissa, set modified characteristic  
            nSteps += 3;
        }
    }
    return nSteps;
}

int bAccNegComplement; // flag to signals acc has complemented a negative ass (== sign adjust) 
                       // needed to compute execution cycles taken by the intruction

// add to accumulator, set Overflow
void AddToAcc(t_int64 a1, t_int64 a0, int bSetOverflow) 
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
    if (bSetOverflow) {
        if ((ACC[1] >= D10) || (ACC[1] <= -D10)) { 
            ACC[1] = ACC[1] % D10; 
            OV=1; 
        }
    }
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

// normalize to 4 digits, 10 complements
void NormalizeAddr(int * addr)
{
    while (*addr >= 10000) *addr -= 10000; 
    while (*addr < 0) *addr += 10000; 
}

// apply index register to a tagged address
// removes tag, replace value with developed address
// return 1 if address was tagged, and has been replaced by developed addr
int ApplyIndexRegister(int * addr)
{
    int n = 0;
    int norm = 0;

    // check for tag and untag
    if ((*addr >= 2000) && (*addr < 4000)) {n = 1; norm = 2000; } else
    if ((*addr >= 4000) && (*addr < 6000)) {n = 2; norm = 4000; } else
    if ((*addr >= 6000) && (*addr < 8000)) {n = 3; norm = 6000; } else
    if ((*addr >= 9200) && (*addr < 9260)) {n = 1; norm = 200; } else
    if ((*addr >= 9400) && (*addr < 9460)) {n = 2; norm = 400; } else
    if ((*addr >= 9600) && (*addr < 9660)) {n = 3; norm = 600; } else
    return 0;   // address not tagged

    *addr = *addr + IR[n-1] - norm;
    NormalizeAddr(addr);

    return 1;
}

// opcode decode 
// input: prior to call DecodeOpcode PR cpu register must be loaded with the word to decode 
// output: decoded instruction as opcode, DA, IA parts
//         returns opname: points to opcode name or NULL if undef opcode
CONST char * DecodeOpcode(t_int64 d, int * opcode, int * DA, int * IA)
{
    CONST char * opname;

    *opcode = Shift_Digits(&d, 2);          // current inste opcode
    *DA     = Shift_Digits(&d, 4);          // addr of data used by current instr
    *IA     = Shift_Digits(&d, 4);          // addr of next instr

    opname  = (cpu_unit.flags & OPTION_SOAPMNE) ? base_ops[*opcode].name2 : base_ops[*opcode].name1;
    if (base_ops[*opcode].option == opStorUnit) {
        // opcode available if IBM 653 Storage Unit is present
        if (STOR == 0) return NULL;
    } else if (base_ops[*opcode].option == opCntrlUnit) {
        // opcode available if IBM 652 Control Unit is present
        if (CNTRL == 0) return NULL;
    } 
    return opname;
}

// transfer (copy words) between IAS and DRUM
// dir = "D->I" or "I->D"
// bEOB = 1 -> End of IAS band terminated transfer
// return number of words transfered
int TransferIAS(CONST char * dir, int bEOB)
{
    int n, f0, t0, f1, t1, ec;

    n = f0 = t0 = f1 = t1 = ec = 0;
    while (1) {
        if (dir[0] == 'D') {
            IAS[IAS_TimingRing] = DRUM[AR];
            IAS_NegativeZeroFlag[IAS_TimingRing] = DRUM_NegativeZeroFlag[AR];
            if (n==0) {f0=AR; t0=IAS_TimingRing;}
            f1=AR; t1=IAS_TimingRing;
        } else {
            DRUM[AR] = IAS[IAS_TimingRing];
            DRUM_NegativeZeroFlag[AR] = IAS_NegativeZeroFlag[IAS_TimingRing];
            if (n==0) {t0=AR; f0=IAS_TimingRing;}
            t1=AR; f1=IAS_TimingRing;
        }
        n++;
        if ((AR % 50) == 49)                        { ec = 0; break; }
        if (IAS_TimingRing == 9059)                 { ec = 1; break; }
        if ((bEOB) && ((IAS_TimingRing % 10) == 9)) { ec = 2; break; }
        AR++; IAS_TimingRing++;
    }
    sim_debug(DEBUG_DATA, &cpu_dev, " ... Copy %04d-%04d to %04d-%04d (%d words)\n",
                                               f0, f1, t0, t1, n);
    sim_debug(DEBUG_DATA, &cpu_dev, "     ended by end of %s condition\n",
                                               (ec == 0) ? "Drum band" : (ec == 1) ? "IAS" : "IAS Block");
    IAS_TimingRing = (IAS_TimingRing + 1) % 60; // incr timing ring at end of pch
    return n; 
}


// opcode execution 
// input: opcode, DA (data address), DrumAddr (current word under the r/w heads. Needed to calculate time used on instr execution)
//        prior to call ExecOpcode DIST cpu register must be loaded with the needed data for inst execution
// output: bBranchToDA: =1 if next inst must be taken from DA register instead of DA
//         CpuStepsUsed: number of steps (=word time) used on program execution
t_stat ExecOpcode(int opcode, int DA, 
                  int * bBranchToDA, 
                  int DrumAddr, 
                  int * CpuStepsUsed)
{
    t_stat reason = 0;
    t_int64 d;
    int i, n, neg; 
    int bUsingIAS;

    *bBranchToDA  = 0; 
    *CpuStepsUsed = 0;

    switch(opcode) {
        case OP_NOOP   :   // No operation 
            if ((IC == 0) && ((PR % D4) == 0)) reason = STOP_HALT; // if loop on NOOP on addr zero -> machine idle -> stop cpu
            break;
        case OP_STOP   :   // Stop if console switch is set to stop, otherwise continue as a NO-OP 
            if (CSWProgStop) {
                reason = STOP_PROG;
                // stops has the consequence to prevent AR to be set with IA contents (to point to next instruction).
                // so must set a flag so next setp/go scp command will take next inst to execute from 
                // IA field in PR reg instead of AR
                ProgStopFlag = 1;
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
            AddToAcc(0,d,1);
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
            AddToAcc(d,0,1);
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
                sim_debug(DEBUG_DETAIL, &cpu_dev, "... Mult result ACC: 0000000000 0000000000- (Minus Zero), OV: 0\n");
                // acc set to minus zero 
                ACC[1] = ACC[0] = 0;
                AccNegativeZeroFlag = 1;
                break; 
            }
            *CpuStepsUsed = 0;
            OV = 0;
            neg = (DIST < 0) ? 1:0; if (AccNegative) neg = 1-neg;
            d      = AbsWord(DIST); 
            ACC[0] = AbsWord(ACC[0]); 
            ACC[1] = AbsWord(ACC[1]); 
            for(i=0;i<10;i++) {
                n = ShiftAcc(1);
                *CpuStepsUsed = *CpuStepsUsed + 2;
                while (n-- > 0) {
                    AddToAcc(0, d, 1);
                    *CpuStepsUsed = *CpuStepsUsed + 18;
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
                            +*CpuStepsUsed;                 // i holds the number of loops done
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
                reason = STOP_OV; // divisor zero allways stops the machine
            } else if (AbsWord(DIST) <= AbsWord(ACC[1])) {
                OV = 1;
                sim_debug(DEBUG_DETAIL, &cpu_dev, "Quotient Overflow -> OV set and ERROR\n");
                reason = STOP_OV; // quotient overfow allways stops the machine
            } else {
                *CpuStepsUsed = 0;
                OV = 0;
                neg = (DIST < 0) ? 1:0; if (AccNegative) neg = 1-neg;
                d      = AbsWord(DIST); 
                ACC[0] = AbsWord(ACC[0]); 
                ACC[1] = AbsWord(ACC[1]); 
                for(i=0;i<10;i++) {
                    n = ShiftAcc(1);
                    ACC[1] = ACC[1] + n * D10;
                    *CpuStepsUsed = *CpuStepsUsed + 2;
                    while (d <= ACC[1]) {
                        AddToAcc(-d, 0, 0);
                        *CpuStepsUsed = *CpuStepsUsed + 18;
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
                                +*CpuStepsUsed + 40;            // i holds the number of loops done
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Div result ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            break;
        // shift
        case OP_SLT: // Shift Left
        case OP_SRT: // Shift Right
        case OP_SRD: // Shift Right and Round
            n = DA % 10;                            // number of digits to shift
            if (opcode == OP_SRD) if (n == 0) n=10; // SRD 0000 means 10 sifts. SRT/SLT 0000 means no shifts
            d = 0;
            while (n-- > 0) {
                d = ShiftAcc((opcode == OP_SLT) ? 1:-1);
            }
            if (opcode == OP_SRD) {
                if (d <= - 5) AddToAcc(0,-1,1);
                if (d >=   5) AddToAcc(0,+1,1);
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
            n = 10 - DA % 10;  // shift count (nine's complement of unit digit of DA)
            neg = AccNegative; // save acc sign
            ACC[0] = AbsWord(ACC[0]);
            ACC[1] = AbsWord(ACC[1]);
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
            if (neg) {ACC[0] = -ACC[0]; ACC[1] = -ACC[1]; }
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

            // sequence chart for store
            // (1)       (0/1)      (1)      (0..49)  (1)     (1)        (1)
            // Enable    Wait       L/U acc  Search   Store   IA to AR   Enable PR
            // Dist      for even   to dist  data     data
            //           or odd     
            *CpuStepsUsed = 1+1+1+1+1+                   
                            + (((opcode == OP_STU) ? DrumAddr:DrumAddr+1) % 2);   // wait for odd/even depending on STU/STL opcode
            break;
        case OP_STD: // store distributor
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
            *CpuStepsUsed = 1+1+1+1
                            +(DrumAddr % 2);                 // wait for even 
            break;
        case OP_LD:  // Load Distributor
            *CpuStepsUsed = 1+1+1+1;                    
            break;
        case OP_TLU:   // Table lookup 
            {
                char s[6];
                sim_debug(DEBUG_DETAIL, &cpu_dev, "... Search DIST: %06d%04d%c '%s'\n", 
                            printfd, 
                            word_to_ascii(s, 1, 5, DIST));

                bUsingIAS = (AR >= 9000) ? 1:0;
                if (bUsingIAS) {
                    AR = DA; // if TLU is searching on IAS, search starts at given addr
                } else {
                    AR = (DA / 50) * 50; // set AR to start of drum band based on DA
                }
                AR--; n=-1;
                while (1) {
                    AR++; n++;
                    if (0==IsDrumAddrOk(AR, vda_DS)) {
                        sim_debug(DEBUG_DETAIL, &cpu_dev, "Invalid AR addr %d ERROR\n", AR);
                        reason = STOP_ADDR;
                        break;
                    }
                    if ((bUsingIAS == 0) && ((AR % 50) > 47)) continue; // skip addr 48 & 49 of band that cannot be used for tables
                    ReadAddr(AR, &d, NULL);       // read table argument
                    if (AbsWord(d) >= AbsWord(DIST)) {
                        sim_debug(DEBUG_DETAIL, &cpu_dev, "...  Found %04d: %06d%04d%c '%s'\n", 
                            AR, printfw(d,0), 
                            word_to_ascii(s, 1, 5, d));
                        break; // found
                    }
                }
                // if tlu on ias, incr timing ring at end of instr execution
                if (bUsingIAS) IAS_TimingRing = (IAS_TimingRing + 1) % 60;
                // set the result as xxNNNNxxxx in lower acc
                ACC[0] = SetDA(ACC[0], DA+n);                 
                sim_debug(DEBUG_DETAIL, &cpu_dev, "... Result ACC: %06d%04d %06d%04d%c, OV: %d\n",
                    printfa,
                    OV);
            }
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
                sim_debug(DEBUG_DETAIL, &cpu_dev, "Not Zero -> Branch Taken\n");
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
                sim_debug(DEBUG_DETAIL, &cpu_dev, "Is Negative -> Branch Taken\n");
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
        case OP_RD:   // Read a card 
            bUsingIAS = (AR >= 9000) ? 1:0;
            {
                char s[6];

                if (bUsingIAS == 0) {
                    AR = (DA / 50) * 50 + 1; // Drum Read Band is XX01 to XX10 or XX51 to XX60
                }

                reason = cdr_cmd(&cdr_unit[1], IO_RDS, AR);
                if (reason == SCPE_NOCARDS) {
                    reason = STOP_CARD;
                    break;
                } else if (reason != SCPE_OK) {
                    break;
                }
                // copy card data from IO Sync buffer to drum/ias
                for (i=0;i<10;i++) {
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "... Read Card %04d: %06d%04d%c '%s'\n", 
                        AR+i, printfw(IOSync[i],IOSync_NegativeZeroFlag[i]), 
                        word_to_ascii(s, 1, 5, IOSync[i]));
                    if (bUsingIAS == 0) {
                        DRUM[AR + i] = IOSync[i];
                        DRUM_NegativeZeroFlag[AR + i] = IOSync_NegativeZeroFlag[i];
                    } else {
                        n = AR - 9000 + i;
                        IAS[n] = IOSync[i];
                        IAS_NegativeZeroFlag[n] = IOSync_NegativeZeroFlag[i];
                        if ((n % 10) == 9) break; // hit ias end of block, terminate read even if transfered less than 10 words
                    }                   
                }
                if (bUsingIAS) IAS_TimingRing = DA; // is using ias, set timing ring on instr completition
                if (cdr_unit[1].u5 & URCSTA_LOAD) {
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "... Is a LOAD Card\n");
                    *bBranchToDA = 1;      // load card -> next instr is taken from DA
                }
            }
            // 300 msec read cycle, 270 available for computing
            *CpuStepsUsed = 312; // 30 msec div 0.096 msec word time;                    
            InterLockCount[IL_RD1] = 3120; // 300 msec for read card processing
            break;
        case OP_PCH:   // Punch a card 
            bUsingIAS = (AR >= 9000) ? 1:0;
            {
                char s[6];

                if (bUsingIAS == 0) {
                    AR = (DA / 50) * 50 + 27; // Drum Read Band is XX27 to XX36 or XX77 to XX86
                }

                // clear IO Sync buffer
                for (i=0;i<10;i++) IOSync[i] = IOSync_NegativeZeroFlag[i] = 0;
                // copy card data to IO Sync buffer from drum/ias
                for (i=0;i<10;i++) {
                    if (bUsingIAS == 0) {
                        IOSync[i] = DRUM[AR + i];
                        IOSync_NegativeZeroFlag[i] = DRUM_NegativeZeroFlag[AR + i];
                    } else {
                        n = AR - 9000 + i;
                        IOSync[i] = IAS[n];
                        IOSync_NegativeZeroFlag[i] = IAS_NegativeZeroFlag[n];
                        IAS_TimingRing = i;
                        if ((n % 10) == 9) break; // hit ias end of block, terminate even if transfered less than 10 words
                    }                   
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "... Punch Card %04d: %06d%04d%c '%s'\n", 
                        AR+i, printfw(IOSync[i],IOSync_NegativeZeroFlag[i]), 
                        word_to_ascii(s, 1, 5, IOSync[i]));
                }

                reason = cdp_cmd(&cdp_unit[1], IO_WRS,AR);
                if (reason == SCPE_NOCARDS) {
                    reason = STOP_CARD;
                    break;
                } else if (reason != SCPE_OK) {
                    break;
                }
                if (bUsingIAS) IAS_TimingRing = (IAS_TimingRing + 1) % 60; // incr timing ring at end of pch
            }
            // 600 msec punch cycle, 565 available for computing
            *CpuStepsUsed = 365; // 35 msec div 0.096 msec word time;                    
            InterLockCount[IL_WR1] = 6250; // 600 msec for punch card processing
            break;
        // IAS - Immediate Access Storage
        case OP_SET: // Set IAS Timing Ring
            *CpuStepsUsed = 1+1+1;                    
            break;
        case OP_LDI: // Load IAS (from Drum)
            n = TransferIAS("D->I", 0); // transfer drum to ias, end of ias block does not terminate transfer
            *CpuStepsUsed = 1+1+1+n;                    
            break;
        case OP_STI: // Store IAS (to Drum)
            n = TransferIAS("I->D", 0); // transfer ias to drum, end of ias block does not terminate transfer
            *CpuStepsUsed = 1+1+1+n;                    
            break;
        case OP_LIB: // Load IAS Block (from Drum)
            n = TransferIAS("D->I", 1); // transfer drum to ias, end of ias block does not terminate transfer
            *CpuStepsUsed = 1+1+1+n;                    
            break;
        case OP_SIB: // Store IAS Block (to Drum)
            n = TransferIAS("I->D", 1); // transfer ias to drum, end of ias block does not terminate transfer
            *CpuStepsUsed = 1+1+1+n;                    
            break;
        // Index Register
        case OP_AXA: // Add/Substract [with reset] to IRA
        case OP_SXA:
        case OP_RAA:
        case OP_RSA:
            n = IR[0];
            if  ((opcode == OP_RAA) || (opcode == OP_RSA)) n = 0;
            if (DA >= 8000) {
               ReadAddr(DA, &d, NULL);
               i = (int) (d % D4); 
            } else {
               i = DA;
            }
            n = n + (((opcode == OP_AXA) || (opcode == OP_RAA)) ? i : -i);
            NormalizeAddr(&n);
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... IRA: %04d\n", 
                                                        n);
            IR[0] = n;
            *CpuStepsUsed = 1+1+1;                    
            break;
        case OP_AXB: // Add/Substract [with reset] to IRB
        case OP_SXB:
        case OP_RAB:
        case OP_RSB:
            n = IR[1];
            if  ((opcode == OP_RAB) || (opcode == OP_RSB)) n = 0;
            if (DA >= 8000) {
               ReadAddr(DA, &d, NULL);
               i = (int) (d % D4); 
            } else {
               i = DA;
            }
            n = n + (((opcode == OP_AXB) || (opcode == OP_RAB)) ? i : -i);
            NormalizeAddr(&n);
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... IRB: %04d\n", 
                                                        n);
            IR[1] = n;
            *CpuStepsUsed = 1+1+1;                    
            break;
        case OP_AXC: // Add/Substract [with reset] to IRC
        case OP_SXC:
        case OP_RAC:
        case OP_RSC:
            n = IR[2];
            if  ((opcode == OP_RAC) || (opcode == OP_RSC)) n = 0;
            if (DA >= 8000) {
               ReadAddr(DA, &d, NULL);
               i = (int) (d % D4); 
            } else {
               i = DA;
            }
            n = n + (((opcode == OP_AXC) || (opcode == OP_RAC)) ? i : -i);
            NormalizeAddr(&n);
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... IRC: %04d\n", 
                                                        n);
            IR[2] = n;
            *CpuStepsUsed = 1+1+1;                    
            break;
        case OP_BMA: // Branch on IR Minus
        case OP_BMB:
        case OP_BMC:
            i = ((opcode == OP_BMA) ? 0 : (opcode == OP_BMB) ? 1 : 2);
            n = IR[i];
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... IR%c: %04d\n", 
                                                      i+'A', n);
            if (n<0) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "Is Negative -> Branch Taken\n");
                *bBranchToDA = 1; 
            }
            *CpuStepsUsed = 1+1                    
                            + ((*bBranchToDA) ? 1:0);   // one extra step needed if branch taken
            break;
        case OP_NZA: // Branch on IR Zero
        case OP_NZB:
        case OP_NZC:
            i = ((opcode == OP_NZA) ? 0 : (opcode == OP_NZB) ? 1 : 2);
            n = IR[i];
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... IR%c: %04d\n", 
                                                      i+'A', n);
            if (n==0) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "Is Zero -> Branch Taken\n");
                *bBranchToDA = 1; 
            }
            *CpuStepsUsed = 1+1                    
                            + ((*bBranchToDA) ? 1:0);   // one extra step needed if branch taken
            break;
        // floating point
        case OP_FAD: // FP Add
        case OP_UFA: // Unnormalized FP Add
        case OP_FSB: // FP Sub
        case OP_FAM: // FP Add Absolute value
        case OP_FSM: // FP Sub Absolute
            n = AddFloatToAcc((opcode == OP_FSB) || (opcode == OP_FSM), // subtract?
                              (opcode == OP_FAM) || (opcode == OP_FSM), // absolute value?
                              (opcode != OP_UFA) // normalize?
                              );
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... ACC: %06d%04d %06d%04d%c, OV: %d, DIST: %06d%04d%c\n", 
                printfa, OV, printfd);
            *CpuStepsUsed = 1+1
                            +(DrumAddr % 2)             // using upper acc -> wait for even
                            +2+2+2+1
                            +n;                         // Float Add steps 
            break;
        case OP_FMP: // Float Multiply
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Mult ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            sim_debug(DEBUG_DETAIL, &cpu_dev,   "...  by DIST: %06d%04d%c\n", 
                                                               printfd);
            OV = 0;
            if (((ACC[1] / 100) == 0) || ((DIST / 100) == 0)) {
                // if any mantissa is zero -> multiply by zero -> result = 0
                ACC[1] = ACC[0] = 0;
            } else {
                int exp = GetExp(DIST) + GetExp(ACC[1]) - 50;

                neg = (DIST < 0) ? -1:1; if (AccNegative) neg = -neg;
                ACC[1] = SetExp(AbsWord(ACC[1]), 0); 
                d      = SetExp(AbsWord(DIST),   0); 
                // mult mantissas
                for(i=0;i<10;i++) {
                    n = ShiftAcc(1);
                    *CpuStepsUsed = *CpuStepsUsed + 2;
                    while (n-- > 0) {
                        AddToAcc(0, d, 1);
                        *CpuStepsUsed = *CpuStepsUsed + 18;
                        if (OV) break;
                    }
                    if (OV) break;
                }
                MantissaRoundAndNormalizeToFloat(CpuStepsUsed, neg, exp);
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... FP Mult result ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa, 
                OV);
            *CpuStepsUsed = 1+1+2+2+2+1+ *CpuStepsUsed 
                            +(DrumAddr % 2);                // wait for even 
            break;        
        case OP_FDV: // Float Divide 
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... Div ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... by DIST: %06d%04d%c\n", 
                                                               printfd);
            OV = 0;
            if ((DIST / 100) == 0) {    // check mantissa for zero, not exponent
                OV = 1;
                sim_debug(DEBUG_DETAIL, &cpu_dev, "Divide By Zero -> OV set and ERROR\n");
                reason = STOP_OV; // float divisor zero allways stops the machine
            } else if ((ACC[1] / 100) == 0) {
                // if dividend is zero -> result = 0
                ACC[1] = ACC[0] = 0;
            } else {
                int exp = GetExp(ACC[1]) - GetExp(DIST) + 50;

                neg = (DIST < 0) ? -1:1; if (AccNegative) neg = -neg;

                ACC[1] = AbsWord(ACC[1]) / 100; 
                d      = AbsWord(DIST)   / 100; 
                // div mantissas 
                for(i=0;;i++) {
                    while (d <= ACC[1]) {
                        AddToAcc(-d, 0, 0);
                        *CpuStepsUsed = *CpuStepsUsed + 18;
                        ACC[0] = ACC[0] + 10; // add to second position of lower
                    }
                    if (i > 8) break;
                    if ((i == 8) && (Get_HiDigit(ACC[0]))) {exp++; break;}
                    n = ShiftAcc(1);
                    ACC[1] = ACC[1] + n * D10; // extra digit
                    *CpuStepsUsed = *CpuStepsUsed + 2;
                }
                ACC[1] = ACC[0];
                MantissaRoundAndNormalizeToFloat(CpuStepsUsed, neg, exp);
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev, "... FP Div result ACC: %06d%04d %06d%04d%c, OV: %d\n", 
                printfa,
                OV);
            *CpuStepsUsed = 1+1+2+2+16+2+1+ *CpuStepsUsed 
                            +(DrumAddr % 2);                // wait for even 
            break;
        default:
            reason = STOP_UUO;
            break;
    }
    if ((reason == 0) && (OV) && (CSWOverflowStop)) reason = STOP_OV;

    return reason;
}

// return 1 if must wait for storage 
int WaitForStorage(int AR)
{
   if ((AR >= 0) && (AR < DRUMSIZE)) {
       if ((AR % 50) != DrumAddr) return 1; // yes, must wait for drum 
   } else if ((STOR) && (AR >= 9000) && (AR < 9060)) {
       if (InterLockCount[IL_IAS] > 0) return 1; // yes, IAS was interlocked. Must wait until interlock is released
   }
   return 0;
}

t_stat
sim_instr(void)
{
    t_stat              reason;
    int                 opcode, halt_cpu;
    int                 bReadData, bWriteDrum, bBranchToDA;
    int                 instr_count = 0; /* Number of instructions to execute */
    const char *        opname;          /* points to opcode name */               

    int IA = 0;                                         // Instr Address: addr of next inst 
    int DA = 0;                                         // Data Address; addr of data to be used by current inst

    int MachineCycle, CpuStepsUsed, il, WaitForInterlock;

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
    0                I-Cycle        WAIT FOR INSTR: 
                                       wait for drum to be positioned at address given by AR cpu register 
    1                I-Cycle        FETCH INST: 
                                       read the drum to get instr to PR register, 
                                       decode PR as opcode, DA, IA, 
                                       apply index tags if needed, write back to PR
                                       check if opcode must wait for interlock release
                                       check if opcode reads data from drum 
    2                D-Cycle        WAIT FOR DATA READ:
                                       wait for interlock release if needed
                                       wait for drum to be positioned at AR address if decoded opcode reads data from drum
    3                D-Cycle        EXEC:
                                       get data from storage into DIST if needed
                                       set interlock if needed
                                       execute opcode operation
    4                D-Cycle        WAIT FOR DATA WRITE:
                                       wait opcode excution time 
                                       wait for drum to be positioned at AR address if executed opcode writes data to drum
    5                D-Cycle        WRITEBACK:
                                       if executed opcode writes data to drum, write DIST to drum 
                                       set AR=IA to read next instruction 

    */

    if (sim_step != 0) {
        instr_count = sim_step;
        sim_cancel_step();
    }

    reason = halt_cpu = 0;

    MachineCycle = CpuStepsUsed = 0;
    DrumAddr = 0;
    CpuStepsUsed = 0;

    if ((ProgStopFlag) && 
        // if last inst was a programmed stop, 
        // and AR has not been changed (still contains the same value set by stop 01 opcode)
        // gets instr to execute from IA instead of AR. This is to simulate the D-Cycle on stop opcode resume
        ((PR / D8) == 01) &&
        (AR == ((PR / D4) % D4))) {
        AR = (PR % D4);
        ProgStopFlag = 0;
    }

    WaitForInterlock = 0; // clear interlocks
    for (il=0;il<IL_array;il++) InterLockCount[il] = 0;

    sim_cancel (&cpu_unit);
    sim_activate (&cpu_unit, 1);     

    while (reason == 0) {       /* loop until halted */

        if (sim_interval <= 0) {        /* event queue? */
            reason = sim_process_event();
            if (reason == SCPE_STOP) {
                reason = 0;    // if stop cpu requested, does not do it inmediatelly
                halt_cpu = 1;  // signal it so cpu is halted on end of current intr execution cycle
            }
            if (reason != SCPE_OK) {
                break;      
            }
        }
        // housekeeping at beggining of inst execution cycle
        if (MachineCycle == 0) {
            // save current instr addr in pseudoregister IC
            IC = AR;
            // init current instr opcode in pseudoregister PROP
            PROP = 0;
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
        }

        /* Main instruction fetch/decode loop */
        sim_interval -= 1;         /* count down */

        // simulate the rotating drum: incr current drum position
        DrumAddr = (DrumAddr+1) % 50;
        // if any interlock set, decrease it 
        for (il=0;il<IL_array;il++) if (InterLockCount[il] > 0) InterLockCount[il]--;
        // decrease pending to execute step intruction count
        if (CpuStepsUsed > 0) CpuStepsUsed--;

        // WAIT FOR INSTR
        if (MachineCycle == 0) {
            if (HalfCycle == 2 ) {       // if D-Half should start
                HalfCycle = 1;           // bump half cycle to exec I-Half on next scp step
                instr_count = 1;         // break at the end of D-half execution
                MachineCycle = 3;
                continue;
            }
            // should wait for storage to fetch inst?
            if (FAST == 0) if (WaitForStorage(AR)) continue; // yes
            // init inst execution
            CpuStepsUsed = 0; 

            MachineCycle = 1; 
        }
        // FETCH INST
        if (MachineCycle == 1) {
            // get current intruction from storage, save current instr addr in IC
            IC = AR;
            if (0==ReadAddr(AR, &PR, NULL)) {
                reason = STOP_ADDR; 
                goto end_of_cycle;
            }
            // decode inst
            opname = DecodeOpcode(PR, &opcode, &DA, &IA);
            sim_debug(DEBUG_CMD, &cpu_dev, "Exec %04d: %02d %-6s %04d %04d %s%s\n", 
                                           IC, opcode, (opname == NULL) ? "???":opname, DA, IA,
                                           ((AR >= MAXDRUMSIZE) || (DRUM_Symbolic_Buffer[AR * 80] == 0)) ? "" : "            symb: ", 
                                           (AR >= MAXDRUMSIZE) ? "" : &DRUM_Symbolic_Buffer[AR * 80]);
            PROP = (uint16) opcode;
            if (opname == NULL) {
                reason = STOP_UUO; 
                goto end_of_cycle;
            }
            // if DA or IA tagged, modify DA or IA to remove tag and set the developed address in PR
            if (STOR) {
                int nIndexsApplied; 
                nIndexsApplied = ApplyIndexRegister(&DA) + ApplyIndexRegister(&IA);
                if (nIndexsApplied > 0) {
                    CpuStepsUsed += nIndexsApplied;
                    PR = (t_int64) opcode * D8 + (t_int64) DA * D4 + (t_int64) IA;
                    sim_debug(DEBUG_CMD, &cpu_dev, "Exec %04d: %02d %-6s %04d %04d %s\n", 
                                           IC, opcode, (opname == NULL) ? "???":opname, DA, IA, 
                                           " (developed addr)");
                }
            }

            AR = DA;    // allways trasnfer DA to AR even if drum will be not read. This is why 
                        // all opcodes must have a valid DA address even if not used to read drum (eg SRT 0003 to shift)

    
            // simulates the machine working on half cycles
            if (HalfCycle == 1) {      // if I-Half finished, about to exec D-Half
                HalfCycle = 2;                                   // bump half cycle to exec D-Half on next scp step
                reason = SCPE_STEP;                              // then break beacuse I-Half finished
                break; 
            } 

            bReadData = (base_ops[opcode].opRW & opReadDA) ? 1:0;

            // check if opcode should wait for and already set interlock
            WaitForInterlock = base_ops[opcode].opInterLock;

            MachineCycle = 2; 
        }
        // WAIT FOR DATA READ
        if (MachineCycle == 2) {
            // should wait to exec the inst (the address untagging) ?
            if (FAST == 0) if (CpuStepsUsed > 0) continue; // yes
            // should wait for interlock release for opcode execution?
            if (WaitForInterlock) {
                if (FAST == 0) if (InterLockCount[WaitForInterlock] > 0) continue; // interlock makes execution wait
                InterLockCount[WaitForInterlock] = 0; // clear interlock
                WaitForInterlock = 0;
            }
            // should wait for storage to fetch data?
            if (bReadData) {
                if (FAST == 0) if (WaitForStorage(AR)) continue; // yes
            }

            MachineCycle = 3;             
        }
        // EXEC
        if (MachineCycle == 3) {
            // decode again PR register to reload internal register DA, IA, AR again. Needed if we are executing half cycles  
            opname = DecodeOpcode(PR, &opcode, &DA, &IA);
            AR = DA;
            if (opname == NULL) {
                reason = STOP_UUO; 
                goto end_of_cycle;
            }
            // even if no data is fetched, DA addr must be a valid one for this opcode
            if (0==IsDrumAddrOk(AR, base_ops[opcode].validDA)) {  
               sim_debug(DEBUG_DETAIL, &cpu_dev, "... %04d: Invalid addr ERROR\n", AR);
               reason = STOP_ADDR;
               goto end_of_cycle;
            }
            // get data from if needed
            bReadData = (base_ops[opcode].opRW & opReadDA) ? 1:0;
            if (bReadData) {
                ReadAddr(AR, &DIST, &DistNegativeZeroFlag);
                sim_debug(DEBUG_DATA, &cpu_dev, "... Read %04d: %06d%04d%c\n", 
                                                           AR,   printfd);
            }
            bWriteDrum = (base_ops[opcode].opRW & opWriteDA) ? 1:0;

            reason = ExecOpcode(opcode, DA, 
                                &bBranchToDA, 
                                DrumAddr, &CpuStepsUsed);
            if (reason != 0) goto end_of_cycle;

            if (bBranchToDA) IA = DA; 

            MachineCycle = 4;
        }
        // WAIT FOR DATA WRITE
        if (MachineCycle == 4) {
            // should wait to exec the inst (opcode execution) ?
            if (FAST == 0) if (CpuStepsUsed > 0) continue; // yes
            // should wait for storage to store data?
            if (bWriteDrum) {
               if (FAST == 0) if (WaitForStorage(AR)) continue; // yes
            }

            MachineCycle = 5; 
        }
        // WRITEBACK
        if (MachineCycle == 5) {
            if (bWriteDrum) {
                sim_debug(DEBUG_DATA, &cpu_dev, "... Write %04d: %06d%04d%c\n",  
                                                            AR,   printfd);
                if (0==WriteAddr(AR, DIST, DistNegativeZeroFlag)) {
                    reason = STOP_ADDR; 
                    goto end_of_cycle;
                }
            }
            // set AR to point to next instr
            AR = IA;        
            // no more machine cycles
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
    ProgStopFlag = 0;
    AccNegativeZeroFlag = 0;
    DistNegativeZeroFlag = 0;
    IC = 0;
    IAS_TimingRing = 0;
    IR[0] = IR[1] = IR[2] = 0;

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
    int NegZero;

    if (val == NEGZERO_value) {
        d = 0;
        NegZero = 1;
    } else {
        d = val;
        NegZero = 0;
    }

    if (0==WriteAddr(addr, d, NegZero)) {
        return SCPE_NXM;
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
    if ((v <= 0) || (v > MAXDRUMSIZE))
        return SCPE_ARG;
    if (v < 4000) {
        for (i = v; i < MAXDRUMSIZE; i++) {
            if ((DRUM[i] != 0) || (DRUM_NegativeZeroFlag[i] != 0)) {
               mc = 1;
               break;
            }
        }
    }
    if ((mc != 0) && (!get_yn("Really truncate memory [N]? ", FALSE)))
        return SCPE_OK;
    cpu_unit.flags &= ~UNIT_MSIZE;
    cpu_unit.flags |= val;
    cpu_unit.capac = 9990 + (v / 1000);
    for (i=0;i<MAXDRUMSIZE * 80;i++) 
        DRUM_Symbolic_Buffer[i] = 0;                // clear drum symbolic info
    for (i = DRUMSIZE; i < MAXDRUMSIZE; i++)
        DRUM[i] = DRUM_NegativeZeroFlag[i] = 0;
    for(i = 0; i < 60; i++) IAS[i] = IAS_NegativeZeroFlag[i] = 0;
    return SCPE_OK;
}


t_stat
cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr) {
    fprintf (st, "These switches are recognized when examining or depositing in CPU memory:\r\n\r\n");
    fprintf (st, "      -c      examine/deposit characters, 5 per word\r\n");
    fprintf (st, "      -m      examine/deposit IBM 650 instructions\r\n\r\n");
    fprintf (st, "The memory of the CPU can be set to 1000, 2000 or 4000 words.\r\n\r\n");
    fprintf (st, "   sim> SET CPU nK\r\n\r\n");
    fprintf (st, "   sim> SET CPU StorageUnit     enables IBM 652 Storage Unit\n");
    fprintf (st, "   sim> SET CPU NoStorageUnit   disables IBM 652 Storage Unit\n\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char * cpu_description (DEVICE *dptr) {
    return "IBM 650 CPU";
}

