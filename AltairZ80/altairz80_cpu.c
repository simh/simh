/*  altairz80_cpu.c: MITS Altair CPU (8080 and Z80)

    Copyright (c) 2002-2014, Peter Schorn

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
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of Peter Schorn shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from Peter Schorn.

    Based on work by Charles E Owen (c) 1997
    Code for Z80 CPU from Frank D. Cringle ((c) 1995 under GNU license)
*/

#include "altairz80_defs.h"
#include <ctype.h>
#define SWITCHCPU_DEFAULT 0xfd

/* Debug flags */
#define IN_MSG          (1 << 0)
#define OUT_MSG         (1 << 1)

#if defined (_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#define PCQ_SIZE        64                      /* must be 2**n                     */
#define PCQ_SIZE_LOG2   6                       /* log2 of PCQ_SIZE                 */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY(PC)   if (pcq[pcq_p] != (PC)) { pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = (PC); }

#define FLAG_C  1
#define FLAG_N  2
#define FLAG_P  4
#define FLAG_H  16
#define FLAG_Z  64
#define FLAG_S  128

#define SETFLAG(f,c)    AF = (c) ? AF | FLAG_ ## f : AF & ~FLAG_ ## f
#define TSTFLAG(f)      ((AF & FLAG_ ## f) != 0)

#define LOW_DIGIT(x)     ((x) & 0xf)
#define HIGH_DIGIT(x)    (((x) >> 4) & 0xf)
#define LOW_REGISTER(x)  ((x) & 0xff)
#define HIGH_REGISTER(x) (((x) >> 8) & 0xff)

#define SET_LOW_REGISTER(x, v)   x = (((x) & 0xff00) | ((v) & 0xff))
#define SET_HIGH_REGISTER(x, v)  x = (((x) & 0xff) | (((v) & 0xff) << 8))

#define PARITY(x)   parityTable[(x) & 0xff]
/*  SET_PV and SET_PV2 are used to provide correct PARITY flag semantics for the 8080 in cases
    where the Z80 uses the overflow flag
*/
#define SET_PVS(s) ((chiptype == CHIP_TYPE_Z80) ? (((cbits >> 6) ^ (cbits >> 5)) & 4) : (PARITY(s)))
#define SET_PV (SET_PVS(sum))
#define SET_PV2(x) ((chiptype == CHIP_TYPE_Z80) ? (((temp == (x)) << 2)) : (PARITY(temp)))

/*  CHECK_CPU_8080 must be invoked whenever a Z80 only instruction is executed
    In case a Z80 instruction is executed on an 8080 the following two cases exist:
    1) Trapping is enabled: execution stops
    2) Trapping is not enabled: decoding continues with the next byte, i.e. interpret as NOP
    Note: in some cases different instructions need to be chosen on 8080
*/
#define CHECK_CPU_8080                                      \
    if (chiptype == CHIP_TYPE_8080) {                       \
        if (cpu_unit.flags & UNIT_CPU_OPSTOP) {             \
            reason = STOP_OPCODE;                           \
            goto end_decode;                                \
        }                                                   \
        else {                                              \
            sim_brk_pend[0] = FALSE;                        \
            continue;                                       \
        }                                                   \
    }

/* CHECK_CPU_Z80 must be invoked whenever a non Z80 instruction is executed */
#define CHECK_CPU_Z80                                       \
    if (cpu_unit.flags & UNIT_CPU_OPSTOP) {                 \
        reason = STOP_OPCODE;                               \
        goto end_decode;                                    \
    }

#define POP(x)  {                                           \
    register uint32 y = RAM_PP(SP);                         \
    x = y + (RAM_PP(SP) << 8);                              \
}

#define JPC(cond) {                                         \
    tStates += 10;                                          \
    if (cond) {                                             \
        PCQ_ENTRY(PCX);                                     \
        PC = GET_WORD(PC);                                  \
    }                                                       \
    else {                                                  \
        PC += 2;                                            \
    }                                                       \
}

#define CALLC(cond) {                                       \
    if (cond) {                                             \
        register uint32 adrr = GET_WORD(PC);                \
        CHECK_BREAK_WORD(SP - 2);                           \
        PUSH(PC + 2);                                       \
        PCQ_ENTRY(PCX);                                     \
        PC = adrr;                                          \
        tStates += 17;                                      \
    }                                                       \
    else {                                                  \
        sim_brk_pend[0] = FALSE;                            \
        PC += 2;                                            \
        tStates += (chiptype == CHIP_TYPE_8080 ? 11 : 10);  \
    }                                                       \
}

extern int32 sio0s      (const int32 port, const int32 io, const int32 data);
extern int32 sio0d      (const int32 port, const int32 io, const int32 data);
extern int32 sio1s      (const int32 port, const int32 io, const int32 data);
extern int32 sio1d      (const int32 port, const int32 io, const int32 data);
extern int32 dsk10      (const int32 port, const int32 io, const int32 data);
extern int32 dsk11      (const int32 port, const int32 io, const int32 data);
extern int32 dsk12      (const int32 port, const int32 io, const int32 data);
extern int32 netStatus  (const int32 port, const int32 io, const int32 data);
extern int32 netData    (const int32 port, const int32 io, const int32 data);
extern int32 nulldev    (const int32 port, const int32 io, const int32 data);
extern int32 hdsk_io    (const int32 port, const int32 io, const int32 data);
extern int32 simh_dev   (const int32 port, const int32 io, const int32 data);
extern int32 sr_dev     (const int32 port, const int32 io, const int32 data);
extern void install_ALTAIRbootROM(void);
extern void do_SIMH_sleep(void);
extern void prepareMemoryAccessMessage(const t_addr loc);
extern void prepareInstructionMessage(const t_addr loc, const uint32 op);

extern t_stat sim_instr_nommu(void);
extern uint8 MOPT[MAXBANKSIZE];
extern t_stat sim_instr_8086(void);
extern void cpu8086reset(void);

/* function prototypes */
static t_stat cpu_set_switcher  (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_reset_switcher(UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_show_switcher (FILE *st, UNIT *uptr, int32 val, void *desc);
static int32 switchcpu_io       (const int32 port, const int32 io, const int32 data);

static t_stat cpu_set_altairrom     (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_set_noaltairrom   (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_set_nommu         (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_set_banked        (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_set_nonbanked     (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_set_ramtype       (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_set_chiptype      (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_set_size          (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_set_memory        (UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat cpu_clear_command     (UNIT *uptr, int32 value, char *cptr, void *desc);
static void cpu_clear(void);
static t_stat cpu_show              (FILE *st, UNIT *uptr, int32 val, void *desc);
static t_stat chip_show             (FILE *st, UNIT *uptr, int32 val, void *desc);
static t_stat cpu_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
static t_stat cpu_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw);
static t_stat cpu_reset(DEVICE *dptr);
static t_stat sim_instr_mmu(void);
static uint32 GetBYTE(register uint32 Addr);
static void PutWORD(register uint32 Addr, const register uint32 Value);
static void PutBYTE(register uint32 Addr, const register uint32 Value);
void out(const uint32 Port, const uint32 Value);
uint32 in(const uint32 Port);
void altairz80_init(void);
t_stat sim_instr(void);
t_stat install_bootrom(int32 bootrom[], int32 size, int32 addr, int32 makeROM);
uint8 GetBYTEWrapper(const uint32 Addr);
void PutBYTEWrapper(const uint32 Addr, const uint32 Value);
uint8 GetByteDMA(const uint32 Addr);
void PutByteDMA(const uint32 Addr, const uint32 Value);
int32 getBankSelect(void);
void setBankSelect(const int32 b);
uint32 getClockFrequency(void);
void setClockFrequency(const uint32 Value);
uint32 getCommon(void);
uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

void PutBYTEExtended(register uint32 Addr, const register uint32 Value);
uint32 GetBYTEExtended(register uint32 Addr);
void cpu_raise_interrupt(uint32 irq);

/*  CPU data structures
    cpu_dev CPU device descriptor
    cpu_unit    CPU unit descriptor
    cpu_reg CPU register list
    cpu_mod CPU modifiers list
*/

UNIT cpu_unit = {
    UDATA (NULL, UNIT_FIX | UNIT_BINK | UNIT_CPU_ALTAIRROM |
    UNIT_CPU_STOPONHALT | UNIT_CPU_MMU, MAXBANKSIZE)
};

        uint32 PCX              = 0;                /* external view of PC                          */
        int32 AF_S;                                 /* AF register                                  */
        int32 BC_S;                                 /* BC register                                  */
        int32 DE_S;                                 /* DE register                                  */
        int32 HL_S;                                 /* HL register                                  */
        int32 IX_S;                                 /* IX register                                  */
        int32 IY_S;                                 /* IY register                                  */
        int32 PC_S              = 0;                /* 8080 / Z80 program counter                   */
        int32 PCX_S             = 0xFFFF0;          /* 8086 program counter                         */
        int32 SP_S;                                 /* SP register                                  */
        int32 AF1_S;                                /* alternate AF register                        */
        int32 BC1_S;                                /* alternate BC register                        */
        int32 DE1_S;                                /* alternate DE register                        */
        int32 HL1_S;                                /* alternate HL register                        */
        int32 IFF_S;                                /* Interrupt Flip Flop                          */
        int32 IR_S;                                 /* Interrupt (upper) / Refresh (lower) register */
        int32 AX_S;                                 /* AX register (8086)                           */
        int32 BX_S;                                 /* BX register (8086)                           */
        int32 CX_S;                                 /* CX register (8086)                           */
        int32 DX_S;                                 /* DX register (8086)                           */
        int32 CS_S;                                 /* CS register (8086)                           */
        int32 DS_S;                                 /* DS register (8086)                           */
        int32 ES_S;                                 /* ES register (8086)                           */
        int32 SS_S;                                 /* SS register (8086)                           */
        int32 DI_S;                                 /* DI register (8086)                           */
        int32 SI_S;                                 /* SI register (8086)                           */
        int32 BP_S;                                 /* BP register (8086)                           */
        int32 SPX_S;                                /* SP register (8086)                           */
        int32 IP_S;                                 /* IP register (8086)                           */
        int32 FLAGS_S;                              /* flags register (8086)                        */
        int32 SR                = 0;                /* switch register                              */
static  int32 bankSelect        = 0;                /* determines selected memory bank              */
static  uint32 common           = 0xc000;           /* addresses >= 'common' are in common memory   */
static  uint32 previousCapacity = MAXBANKSIZE;      /* safe for previous memory capacity            */
static  uint32 clockFrequency   = 0;                /* in kHz, 0 means as fast as possible          */
static  uint32 sliceLength      = 10;               /* length of time-slice for CPU speed           */
                                                    /* adjustment in milliseconds                   */
static  uint32 executedTStates  = 0;                /* executed t-states                            */
static  uint16 pcq[PCQ_SIZE]    = { 0 };            /* PC queue                                     */
static  int32 pcq_p             = 0;                /* PC queue ptr                                 */
static  REG *pcq_r              = NULL;             /* PC queue reg ptr                             */

/* data structure for IN/OUT instructions */
struct idev {
    int32 (*routine)(const int32, const int32, const int32);
};

static  int32 switcherPort      = SWITCHCPU_DEFAULT;
static struct idev oldSwitcherDevice = { NULL };

REG cpu_reg[] = {
    { HRDATA (AF,       AF_S,               16)                                 },
    { HRDATA (BC,       BC_S,               16)                                 },
    { HRDATA (DE,       DE_S,               16)                                 },
    { HRDATA (HL,       HL_S,               16)                                 },
    { HRDATA (IX,       IX_S,               16)                                 },
    { HRDATA (IY,       IY_S,               16)                                 },
    { HRDATA (PC,       PC_S,               16 + MAXBANKSLOG2)                  }, /* 8080 / Z80 PC [6] */
    { HRDATA (PCX,      PCX_S,              16 + MAXBANKSLOG2)                  }, /* 8086 PC       [7] */
    { HRDATA (SP,       SP_S,               16)                                 },
    { HRDATA (AF1,      AF1_S,              16)                                 },
    { HRDATA (BC1,      BC1_S,              16)                                 },
    { HRDATA (DE1,      DE1_S,              16)                                 },
    { HRDATA (HL1,      HL1_S,              16)                                 },
    { GRDATA (IFF,      IFF_S, 2, 2, 0)                                         },
    { FLDATA (IR,       IR_S,               8)                                  },
    { HRDATA (AX,       AX_S,               16)                                 }, /* 8086                      */
    { GRDATA (AL,       AX_S, 16,           8, 0)                               }, /* 8086, low 8 bits of AX    */
    { GRDATA (AH,       AX_S, 16,           8, 8)                               }, /* 8086, high 8 bits of AX   */
    { HRDATA (BX,       BX_S,               16)                                 }, /* 8086                      */
    { GRDATA (BL,       BX_S, 16,           8, 0)                               }, /* 8086, low 8 bits of BX    */
    { GRDATA (BH,       BX_S, 16,           8, 8)                               }, /* 8086, high 8 bits of BX   */
    { HRDATA (CX,       CX_S,               16)                                 }, /* 8086                      */
    { GRDATA (CL,       CX_S, 16,           8, 0)                               }, /* 8086, low 8 bits of CX    */
    { GRDATA (CH,       CX_S, 16,           8, 8)                               }, /* 8086, high 8 bits of CX   */
    { HRDATA (DX,       DX_S,               16)                                 }, /* 8086                      */
    { GRDATA (DL,       DX_S, 16,           8, 0)                               }, /* 8086, low 8 bits of DX    */
    { GRDATA (DH,       DX_S, 16,           8, 8)                               }, /* 8086, high 8 bits of DX   */
    { HRDATA (SPX,      SPX_S,              16)                                 }, /* 8086                      */
    { HRDATA (BP,       BP_S,               16)                                 }, /* 8086, Base Pointer        */
    { HRDATA (SI,       SI_S,               16)                                 }, /* 8086, Source Index        */
    { HRDATA (DI,       DI_S,               16)                                 }, /* 8086, Destination Index   */
    { HRDATA (CS,       CS_S,               16)                                 }, /* 8086, Code Segment        */
    { HRDATA (DS,       DS_S,               16)                                 }, /* 8086, Data Segment        */
    { HRDATA (ES,       ES_S,               16)                                 }, /* 8086, Extra Segment       */
    { HRDATA (SS,       SS_S,               16)                                 }, /* 8086, Stack Segment       */
    { HRDATA (FLAGS,    FLAGS_S,            16)                                 }, /* 8086, FLAGS               */
    { HRDATA (IP,       IP_S,               16),                REG_RO          }, /* 8086, set via PC          */
    { FLDATA (OPSTOP,   cpu_unit.flags,     UNIT_CPU_V_OPSTOP), REG_HRO         },
    { HRDATA (SR,       SR,                 8)                                  },
    { HRDATA (BANK,     bankSelect,         MAXBANKSLOG2)                       },
    { HRDATA (COMMON,   common,             32)                                 },
    { HRDATA (SWITCHERPORT, switcherPort,   8),                                 },
    { DRDATA (CLOCK,    clockFrequency,     32)                                 },
    { DRDATA (SLICE,    sliceLength,        16)                                 },
    { DRDATA (TSTATES,  executedTStates,    32),            REG_RO              },
    { HRDATA (CAPACITY, cpu_unit.capac,     32),            REG_RO              },
    { HRDATA (PREVCAP,  previousCapacity,   32),            REG_RO              },
    { BRDATA (PCQ,      pcq, 16, 16, PCQ_SIZE),             REG_RO + REG_CIRC   },
    { DRDATA (PCQP,     pcq_p,          PCQ_SIZE_LOG2),     REG_HRO             },
    { HRDATA (WRU,      sim_int_char,        8)                                 },
    { NULL }
};

static MTAB cpu_mod[] = {
    { MTAB_XTD | MTAB_VDV,  CHIP_TYPE_8080,     NULL,           "8080",         &cpu_set_chiptype   },
    { MTAB_XTD | MTAB_VDV,  CHIP_TYPE_Z80,      NULL,           "Z80",          &cpu_set_chiptype   },
    { MTAB_XTD | MTAB_VDV,  CHIP_TYPE_8086,     NULL,           "8086",         &cpu_set_chiptype   },
    { UNIT_CPU_OPSTOP,      UNIT_CPU_OPSTOP,    "ITRAP",        "ITRAP",        NULL, &chip_show    },
    { UNIT_CPU_OPSTOP,      0,                  "NOITRAP",      "NOITRAP",      NULL, &chip_show    },
    { UNIT_CPU_STOPONHALT,  UNIT_CPU_STOPONHALT,"STOPONHALT",   "STOPONHALT",   NULL                },
    { UNIT_CPU_STOPONHALT,  0,                  "LOOPONHALT",   "LOOPONHALT",   NULL                },
    { UNIT_CPU_BANKED,      UNIT_CPU_BANKED,    "BANKED",       "BANKED",       &cpu_set_banked     },
    { UNIT_CPU_BANKED,      0,                  "NONBANKED",    "NONBANKED",    &cpu_set_nonbanked  },
    { UNIT_CPU_ALTAIRROM,   UNIT_CPU_ALTAIRROM, "ALTAIRROM",    "ALTAIRROM",    &cpu_set_altairrom  },
    { UNIT_CPU_ALTAIRROM,   0,                  "NOALTAIRROM",  "NOALTAIRROM",  &cpu_set_noaltairrom},
    { UNIT_CPU_VERBOSE,     UNIT_CPU_VERBOSE,   "VERBOSE",      "VERBOSE",      NULL, &cpu_show     },
    { UNIT_CPU_VERBOSE,     0,                  "QUIET",        "QUIET",        NULL                },
    { MTAB_VDV,             0,                  NULL,           "CLEARMEMORY",  &cpu_clear_command  },
    { UNIT_CPU_MMU,         UNIT_CPU_MMU,       "MMU",          "MMU",          NULL                },
    { UNIT_CPU_MMU,         0,                  "NOMMU",        "NOMMU",        &cpu_set_nommu      },
    { MTAB_XTD | MTAB_VDV,  0,                  NULL,           "MEMORY",       &cpu_set_memory     },
    { UNIT_CPU_SWITCHER,    UNIT_CPU_SWITCHER,  "SWITCHER",     "SWITCHER",     &cpu_set_switcher, &cpu_show_switcher   },
    { UNIT_CPU_SWITCHER,    0,                  "NOSWITCHER",   "NOSWITCHER",   &cpu_reset_switcher, &cpu_show_switcher },
    { MTAB_XTD | MTAB_VDV,  0,                  NULL,           "AZ80",         &cpu_set_ramtype    },
    { MTAB_XTD | MTAB_VDV,  1,                  NULL,           "HRAM",         &cpu_set_ramtype    },
    { MTAB_XTD | MTAB_VDV,  2,                  NULL,           "VRAM",         &cpu_set_ramtype    },
    { MTAB_XTD | MTAB_VDV,  3,                  NULL,           "CRAM",         &cpu_set_ramtype    },
    { MTAB_VDV,             4,                  NULL,           "4KB",          &cpu_set_size       },
    { MTAB_VDV,             8,                  NULL,           "8KB",          &cpu_set_size       },
    { MTAB_VDV,             12,                 NULL,           "12KB",         &cpu_set_size       },
    { MTAB_VDV,             16,                 NULL,           "16KB",         &cpu_set_size       },
    { MTAB_VDV,             20,                 NULL,           "20KB",         &cpu_set_size       },
    { MTAB_VDV,             24,                 NULL,           "24KB",         &cpu_set_size       },
    { MTAB_VDV,             28,                 NULL,           "28KB",         &cpu_set_size       },
    { MTAB_VDV,             32,                 NULL,           "32KB",         &cpu_set_size       },
    { MTAB_VDV,             36,                 NULL,           "36KB",         &cpu_set_size       },
    { MTAB_VDV,             40,                 NULL,           "40KB",         &cpu_set_size       },
    { MTAB_VDV,             44,                 NULL,           "44KB",         &cpu_set_size       },
    { MTAB_VDV,             48,                 NULL,           "48KB",         &cpu_set_size       },
    { MTAB_VDV,             52,                 NULL,           "52KB",         &cpu_set_size       },
    { MTAB_VDV,             56,                 NULL,           "56KB",         &cpu_set_size       },
    { MTAB_VDV,             60,                 NULL,           "60KB",         &cpu_set_size       },
    { MTAB_VDV,             64,                 NULL,           "64KB",         &cpu_set_size       },
    { 0 }
};

/* Debug Flags */
static DEBTAB cpu_dt[] = {
    { "LOG_IN",     IN_MSG  },
    { "LOG_OUT",    OUT_MSG },
    { NULL,         0       }
};

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 16, 16, 1, 16, 8,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0,
    cpu_dt, NULL, NULL
};

/*  This is the I/O configuration table. There are 255 possible
    device addresses, if a device is plugged to a port it's routine
    address is here, 'nulldev' means no device is available
*/
static struct idev dev_table[256] = {
    {&nulldev}, {&nulldev}, {&sio0d},   {&sio0s},           /* 00 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 04 */
    {&dsk10},   {&dsk11},   {&dsk12},   {&nulldev},         /* 08 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0C */
    {&sio0s},   {&sio0d},   {&sio1s},   {&sio1d},           /* 10 */
    {&sio0s},   {&sio0d},   {&sio0s},   {&sio0d},           /* 14 */
    {&sio0s},   {&sio0d},   {&nulldev}, {&nulldev},         /* 18 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1C */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 20 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 24 */
    {&netStatus},{&netData},{&netStatus},{&netData},        /* 28 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2C */
    {&nulldev}, {&nulldev}, {&netStatus},{&netData},        /* 30 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 34 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 38 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3C */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 40 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 44 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 48 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 4C */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 50 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 54 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 58 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 5C */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 60 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 64 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 68 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 6C */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 70 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 74 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 78 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 7C */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 80 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 84 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 88 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 8C */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 90 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 94 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 98 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 9C */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* A0 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* A4 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* A8 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* AC */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* B0 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* B4 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* B8 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* BC */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* C0 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* C4 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* C8 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* CC */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* D0 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* D4 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* D8 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* DC */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* E0 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* E4 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* E8 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* EC */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* F0 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* F4 */
    {&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* F8 */
    {&nulldev}, {&hdsk_io}, {&simh_dev}, {&sr_dev}          /* FC */
};

static int32 ramtype = 0;
#define MAX_RAM_TYPE    3

int32 chiptype = CHIP_TYPE_8080;

void out(const uint32 Port, const uint32 Value) {
    if ((cpu_dev.dctrl & OUT_MSG) && sim_deb) {
        fprintf(sim_deb, "CPU: " ADDRESS_FORMAT
                " OUT(port=0x%04x [%5d], value=0x%04x [%5d])\n", PCX, Port, Port, Value, Value);
        fflush(sim_deb);
    }
    dev_table[Port & 0xff].routine(Port, 1, Value);
    if ((cpu_dev.dctrl & OUT_MSG) && sim_deb) {
        fprintf(sim_deb, "CPU: " ADDRESS_FORMAT
                " OUT(port=0x%04x [%5d], value=0x%04x [%5d]) done\n", PCX, Port, Port, Value, Value);
        fflush(sim_deb);
    }
}

uint32 in(const uint32 Port) {
    uint32 result;
    if ((cpu_dev.dctrl & IN_MSG) && sim_deb) {
        fprintf(sim_deb, "CPU: " ADDRESS_FORMAT
            " IN(port=0x%04x [%5d])\n", PCX, Port, Port);
        fflush(sim_deb);
    }
    result = dev_table[Port & 0xff].routine(Port, 0, 0);
    if ((cpu_dev.dctrl & IN_MSG) && sim_deb) {
        fprintf(sim_deb, "CPU: " ADDRESS_FORMAT
            " IN(port=0x%04x [%5d]) = 0x%04x [%5d]\n", PCX, Port, Port, result, result);
        fflush(sim_deb);
    }
    return result;
}

/* the following tables precompute some common subexpressions
    parityTable[i]          0..255  (number of 1's in i is odd) ? 0 : 4
    incTable[i]             0..256! (i & 0xa8) | (((i & 0xff) == 0) << 6) | (((i & 0xf) == 0) << 4)
    decTable[i]             0..255  (i & 0xa8) | (((i & 0xff) == 0) << 6) | (((i & 0xf) == 0xf) << 4) | 2
    cbitsTable[i]           0..511  (i & 0x10) | ((i >> 8) & 1)
    cbitsDup8Table[i]       0..511  (i & 0x10) | ((i >> 8) & 1) | ((i & 0xff) << 8) | (i & 0xa8) |
                                    (((i & 0xff) == 0) << 6)
    cbitsDup16Table[i]      0..511  (i & 0x10) | ((i >> 8) & 1) | (i & 0x28)
    cbits2Table[i]          0..511  (i & 0x10) | ((i >> 8) & 1) | 2
    rrcaTable[i]            0..255  ((i & 1) << 15) | ((i >> 1) << 8) | ((i >> 1) & 0x28) | (i & 1)
    rraTable[i]             0..255  ((i >> 1) << 8) | ((i >> 1) & 0x28) | (i & 1)
    addTable[i]             0..511  ((i & 0xff) << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6)
    subTable[i]             0..255  ((i & 0xff) << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6) | 2
    andTable[i]             0..255  (i << 8) | (i & 0xa8) | ((i == 0) << 6) | 0x10 | parityTable[i]
    xororTable[i]           0..255  (i << 8) | (i & 0xa8) | ((i == 0) << 6) | parityTable[i]
    rotateShiftTable[i]     0..255  (i & 0xa8) | (((i & 0xff) == 0) << 6) | parityTable[i & 0xff]
    incZ80Table[i]          0..256! (i & 0xa8) | (((i & 0xff) == 0) << 6) |
                                    (((i & 0xf) == 0) << 4) | ((i == 0x80) << 2)
    decZ80Table[i]          0..255  (i & 0xa8) | (((i & 0xff) == 0) << 6) |
                                    (((i & 0xf) == 0xf) << 4) | ((i == 0x7f) << 2) | 2
    cbitsZ80Table[i]        0..511  (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) | ((i >> 8) & 1)
    cbitsZ80DupTable[i]     0..511  (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) |
                                    ((i >> 8) & 1) | (i & 0xa8)
    cbits2Z80Table[i]       0..511  (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) | ((i >> 8) & 1) | 2
    cbits2Z80DupTable[i]    0..511  (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) | ((i >> 8) & 1) | 2 |
                                    (i & 0xa8)
    negTable[i]             0..255  (((i & 0x0f) != 0) << 4) | ((i == 0x80) << 2) | 2 | (i != 0)
    rrdrldTable[i]          0..255  (i << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6) | parityTable[i]
    cpTable[i]              0..255  (i & 0x80) | (((i & 0xff) == 0) << 6)
*/

/* parityTable[i] = (number of 1's in i is odd) ? 0 : 4, i = 0..255 */
static const uint8 parityTable[256] = {
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
};

/* incTable[i] = (i & 0xa8) | (((i & 0xff) == 0) << 6) | (((i & 0xf) == 0) << 4), i = 0..256 */
static const uint8 incTable[257] = {
     80,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     16,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     48, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
     48, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
     16,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     16,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     48, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
     48, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
    144,128,128,128,128,128,128,128,136,136,136,136,136,136,136,136,
    144,128,128,128,128,128,128,128,136,136,136,136,136,136,136,136,
    176,160,160,160,160,160,160,160,168,168,168,168,168,168,168,168,
    176,160,160,160,160,160,160,160,168,168,168,168,168,168,168,168,
    144,128,128,128,128,128,128,128,136,136,136,136,136,136,136,136,
    144,128,128,128,128,128,128,128,136,136,136,136,136,136,136,136,
    176,160,160,160,160,160,160,160,168,168,168,168,168,168,168,168,
    176,160,160,160,160,160,160,160,168,168,168,168,168,168,168,168, 80
};

/* decTable[i] = (i & 0xa8) | (((i & 0xff) == 0) << 6) | (((i & 0xf) == 0xf) << 4) | 2, i = 0..255 */
static const uint8 decTable[256] = {
     66,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 26,
      2,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 26,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 58,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 58,
      2,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 26,
      2,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 26,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 58,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 58,
    130,130,130,130,130,130,130,130,138,138,138,138,138,138,138,154,
    130,130,130,130,130,130,130,130,138,138,138,138,138,138,138,154,
    162,162,162,162,162,162,162,162,170,170,170,170,170,170,170,186,
    162,162,162,162,162,162,162,162,170,170,170,170,170,170,170,186,
    130,130,130,130,130,130,130,130,138,138,138,138,138,138,138,154,
    130,130,130,130,130,130,130,130,138,138,138,138,138,138,138,154,
    162,162,162,162,162,162,162,162,170,170,170,170,170,170,170,186,
    162,162,162,162,162,162,162,162,170,170,170,170,170,170,170,186,
};

/* cbitsTable[i] = (i & 0x10) | ((i >> 8) & 1), i = 0..511 */
static const uint8 cbitsTable[512] = {
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
};

/* cbitsDup8Table[i] = (i & 0x10) | ((i >> 8) & 1) | ((i & 0xff) << 8) | (i & 0xa8) |
                        (((i & 0xff) == 0) << 6), i = 0..511 */
static const uint16 cbitsDup8Table[512] = {
    0x0040,0x0100,0x0200,0x0300,0x0400,0x0500,0x0600,0x0700,
    0x0808,0x0908,0x0a08,0x0b08,0x0c08,0x0d08,0x0e08,0x0f08,
    0x1010,0x1110,0x1210,0x1310,0x1410,0x1510,0x1610,0x1710,
    0x1818,0x1918,0x1a18,0x1b18,0x1c18,0x1d18,0x1e18,0x1f18,
    0x2020,0x2120,0x2220,0x2320,0x2420,0x2520,0x2620,0x2720,
    0x2828,0x2928,0x2a28,0x2b28,0x2c28,0x2d28,0x2e28,0x2f28,
    0x3030,0x3130,0x3230,0x3330,0x3430,0x3530,0x3630,0x3730,
    0x3838,0x3938,0x3a38,0x3b38,0x3c38,0x3d38,0x3e38,0x3f38,
    0x4000,0x4100,0x4200,0x4300,0x4400,0x4500,0x4600,0x4700,
    0x4808,0x4908,0x4a08,0x4b08,0x4c08,0x4d08,0x4e08,0x4f08,
    0x5010,0x5110,0x5210,0x5310,0x5410,0x5510,0x5610,0x5710,
    0x5818,0x5918,0x5a18,0x5b18,0x5c18,0x5d18,0x5e18,0x5f18,
    0x6020,0x6120,0x6220,0x6320,0x6420,0x6520,0x6620,0x6720,
    0x6828,0x6928,0x6a28,0x6b28,0x6c28,0x6d28,0x6e28,0x6f28,
    0x7030,0x7130,0x7230,0x7330,0x7430,0x7530,0x7630,0x7730,
    0x7838,0x7938,0x7a38,0x7b38,0x7c38,0x7d38,0x7e38,0x7f38,
    0x8080,0x8180,0x8280,0x8380,0x8480,0x8580,0x8680,0x8780,
    0x8888,0x8988,0x8a88,0x8b88,0x8c88,0x8d88,0x8e88,0x8f88,
    0x9090,0x9190,0x9290,0x9390,0x9490,0x9590,0x9690,0x9790,
    0x9898,0x9998,0x9a98,0x9b98,0x9c98,0x9d98,0x9e98,0x9f98,
    0xa0a0,0xa1a0,0xa2a0,0xa3a0,0xa4a0,0xa5a0,0xa6a0,0xa7a0,
    0xa8a8,0xa9a8,0xaaa8,0xaba8,0xaca8,0xada8,0xaea8,0xafa8,
    0xb0b0,0xb1b0,0xb2b0,0xb3b0,0xb4b0,0xb5b0,0xb6b0,0xb7b0,
    0xb8b8,0xb9b8,0xbab8,0xbbb8,0xbcb8,0xbdb8,0xbeb8,0xbfb8,
    0xc080,0xc180,0xc280,0xc380,0xc480,0xc580,0xc680,0xc780,
    0xc888,0xc988,0xca88,0xcb88,0xcc88,0xcd88,0xce88,0xcf88,
    0xd090,0xd190,0xd290,0xd390,0xd490,0xd590,0xd690,0xd790,
    0xd898,0xd998,0xda98,0xdb98,0xdc98,0xdd98,0xde98,0xdf98,
    0xe0a0,0xe1a0,0xe2a0,0xe3a0,0xe4a0,0xe5a0,0xe6a0,0xe7a0,
    0xe8a8,0xe9a8,0xeaa8,0xeba8,0xeca8,0xeda8,0xeea8,0xefa8,
    0xf0b0,0xf1b0,0xf2b0,0xf3b0,0xf4b0,0xf5b0,0xf6b0,0xf7b0,
    0xf8b8,0xf9b8,0xfab8,0xfbb8,0xfcb8,0xfdb8,0xfeb8,0xffb8,
    0x0041,0x0101,0x0201,0x0301,0x0401,0x0501,0x0601,0x0701,
    0x0809,0x0909,0x0a09,0x0b09,0x0c09,0x0d09,0x0e09,0x0f09,
    0x1011,0x1111,0x1211,0x1311,0x1411,0x1511,0x1611,0x1711,
    0x1819,0x1919,0x1a19,0x1b19,0x1c19,0x1d19,0x1e19,0x1f19,
    0x2021,0x2121,0x2221,0x2321,0x2421,0x2521,0x2621,0x2721,
    0x2829,0x2929,0x2a29,0x2b29,0x2c29,0x2d29,0x2e29,0x2f29,
    0x3031,0x3131,0x3231,0x3331,0x3431,0x3531,0x3631,0x3731,
    0x3839,0x3939,0x3a39,0x3b39,0x3c39,0x3d39,0x3e39,0x3f39,
    0x4001,0x4101,0x4201,0x4301,0x4401,0x4501,0x4601,0x4701,
    0x4809,0x4909,0x4a09,0x4b09,0x4c09,0x4d09,0x4e09,0x4f09,
    0x5011,0x5111,0x5211,0x5311,0x5411,0x5511,0x5611,0x5711,
    0x5819,0x5919,0x5a19,0x5b19,0x5c19,0x5d19,0x5e19,0x5f19,
    0x6021,0x6121,0x6221,0x6321,0x6421,0x6521,0x6621,0x6721,
    0x6829,0x6929,0x6a29,0x6b29,0x6c29,0x6d29,0x6e29,0x6f29,
    0x7031,0x7131,0x7231,0x7331,0x7431,0x7531,0x7631,0x7731,
    0x7839,0x7939,0x7a39,0x7b39,0x7c39,0x7d39,0x7e39,0x7f39,
    0x8081,0x8181,0x8281,0x8381,0x8481,0x8581,0x8681,0x8781,
    0x8889,0x8989,0x8a89,0x8b89,0x8c89,0x8d89,0x8e89,0x8f89,
    0x9091,0x9191,0x9291,0x9391,0x9491,0x9591,0x9691,0x9791,
    0x9899,0x9999,0x9a99,0x9b99,0x9c99,0x9d99,0x9e99,0x9f99,
    0xa0a1,0xa1a1,0xa2a1,0xa3a1,0xa4a1,0xa5a1,0xa6a1,0xa7a1,
    0xa8a9,0xa9a9,0xaaa9,0xaba9,0xaca9,0xada9,0xaea9,0xafa9,
    0xb0b1,0xb1b1,0xb2b1,0xb3b1,0xb4b1,0xb5b1,0xb6b1,0xb7b1,
    0xb8b9,0xb9b9,0xbab9,0xbbb9,0xbcb9,0xbdb9,0xbeb9,0xbfb9,
    0xc081,0xc181,0xc281,0xc381,0xc481,0xc581,0xc681,0xc781,
    0xc889,0xc989,0xca89,0xcb89,0xcc89,0xcd89,0xce89,0xcf89,
    0xd091,0xd191,0xd291,0xd391,0xd491,0xd591,0xd691,0xd791,
    0xd899,0xd999,0xda99,0xdb99,0xdc99,0xdd99,0xde99,0xdf99,
    0xe0a1,0xe1a1,0xe2a1,0xe3a1,0xe4a1,0xe5a1,0xe6a1,0xe7a1,
    0xe8a9,0xe9a9,0xeaa9,0xeba9,0xeca9,0xeda9,0xeea9,0xefa9,
    0xf0b1,0xf1b1,0xf2b1,0xf3b1,0xf4b1,0xf5b1,0xf6b1,0xf7b1,
    0xf8b9,0xf9b9,0xfab9,0xfbb9,0xfcb9,0xfdb9,0xfeb9,0xffb9,
};

/* cbitsDup16Table[i] = (i & 0x10) | ((i >> 8) & 1) | (i & 0x28), i = 0..511 */
static const uint8 cbitsDup16Table[512] = {
     0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8,
    16,16,16,16,16,16,16,16,24,24,24,24,24,24,24,24,
    32,32,32,32,32,32,32,32,40,40,40,40,40,40,40,40,
    48,48,48,48,48,48,48,48,56,56,56,56,56,56,56,56,
     0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8,
    16,16,16,16,16,16,16,16,24,24,24,24,24,24,24,24,
    32,32,32,32,32,32,32,32,40,40,40,40,40,40,40,40,
    48,48,48,48,48,48,48,48,56,56,56,56,56,56,56,56,
     0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8,
    16,16,16,16,16,16,16,16,24,24,24,24,24,24,24,24,
    32,32,32,32,32,32,32,32,40,40,40,40,40,40,40,40,
    48,48,48,48,48,48,48,48,56,56,56,56,56,56,56,56,
     0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8,
    16,16,16,16,16,16,16,16,24,24,24,24,24,24,24,24,
    32,32,32,32,32,32,32,32,40,40,40,40,40,40,40,40,
    48,48,48,48,48,48,48,48,56,56,56,56,56,56,56,56,
     1, 1, 1, 1, 1, 1, 1, 1, 9, 9, 9, 9, 9, 9, 9, 9,
    17,17,17,17,17,17,17,17,25,25,25,25,25,25,25,25,
    33,33,33,33,33,33,33,33,41,41,41,41,41,41,41,41,
    49,49,49,49,49,49,49,49,57,57,57,57,57,57,57,57,
     1, 1, 1, 1, 1, 1, 1, 1, 9, 9, 9, 9, 9, 9, 9, 9,
    17,17,17,17,17,17,17,17,25,25,25,25,25,25,25,25,
    33,33,33,33,33,33,33,33,41,41,41,41,41,41,41,41,
    49,49,49,49,49,49,49,49,57,57,57,57,57,57,57,57,
     1, 1, 1, 1, 1, 1, 1, 1, 9, 9, 9, 9, 9, 9, 9, 9,
    17,17,17,17,17,17,17,17,25,25,25,25,25,25,25,25,
    33,33,33,33,33,33,33,33,41,41,41,41,41,41,41,41,
    49,49,49,49,49,49,49,49,57,57,57,57,57,57,57,57,
     1, 1, 1, 1, 1, 1, 1, 1, 9, 9, 9, 9, 9, 9, 9, 9,
    17,17,17,17,17,17,17,17,25,25,25,25,25,25,25,25,
    33,33,33,33,33,33,33,33,41,41,41,41,41,41,41,41,
    49,49,49,49,49,49,49,49,57,57,57,57,57,57,57,57,
};

/* cbits2Table[i] = (i & 0x10) | ((i >> 8) & 1) | 2, i = 0..511 */
static const uint8 cbits2Table[512] = {
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
};

/* rrcaTable[i] = ((i & 1) << 15) | ((i >> 1) << 8) | ((i >> 1) & 0x28) | (i & 1), i = 0..255 */
static const uint16 rrcaTable[256] = {
    0x0000,0x8001,0x0100,0x8101,0x0200,0x8201,0x0300,0x8301,
    0x0400,0x8401,0x0500,0x8501,0x0600,0x8601,0x0700,0x8701,
    0x0808,0x8809,0x0908,0x8909,0x0a08,0x8a09,0x0b08,0x8b09,
    0x0c08,0x8c09,0x0d08,0x8d09,0x0e08,0x8e09,0x0f08,0x8f09,
    0x1000,0x9001,0x1100,0x9101,0x1200,0x9201,0x1300,0x9301,
    0x1400,0x9401,0x1500,0x9501,0x1600,0x9601,0x1700,0x9701,
    0x1808,0x9809,0x1908,0x9909,0x1a08,0x9a09,0x1b08,0x9b09,
    0x1c08,0x9c09,0x1d08,0x9d09,0x1e08,0x9e09,0x1f08,0x9f09,
    0x2020,0xa021,0x2120,0xa121,0x2220,0xa221,0x2320,0xa321,
    0x2420,0xa421,0x2520,0xa521,0x2620,0xa621,0x2720,0xa721,
    0x2828,0xa829,0x2928,0xa929,0x2a28,0xaa29,0x2b28,0xab29,
    0x2c28,0xac29,0x2d28,0xad29,0x2e28,0xae29,0x2f28,0xaf29,
    0x3020,0xb021,0x3120,0xb121,0x3220,0xb221,0x3320,0xb321,
    0x3420,0xb421,0x3520,0xb521,0x3620,0xb621,0x3720,0xb721,
    0x3828,0xb829,0x3928,0xb929,0x3a28,0xba29,0x3b28,0xbb29,
    0x3c28,0xbc29,0x3d28,0xbd29,0x3e28,0xbe29,0x3f28,0xbf29,
    0x4000,0xc001,0x4100,0xc101,0x4200,0xc201,0x4300,0xc301,
    0x4400,0xc401,0x4500,0xc501,0x4600,0xc601,0x4700,0xc701,
    0x4808,0xc809,0x4908,0xc909,0x4a08,0xca09,0x4b08,0xcb09,
    0x4c08,0xcc09,0x4d08,0xcd09,0x4e08,0xce09,0x4f08,0xcf09,
    0x5000,0xd001,0x5100,0xd101,0x5200,0xd201,0x5300,0xd301,
    0x5400,0xd401,0x5500,0xd501,0x5600,0xd601,0x5700,0xd701,
    0x5808,0xd809,0x5908,0xd909,0x5a08,0xda09,0x5b08,0xdb09,
    0x5c08,0xdc09,0x5d08,0xdd09,0x5e08,0xde09,0x5f08,0xdf09,
    0x6020,0xe021,0x6120,0xe121,0x6220,0xe221,0x6320,0xe321,
    0x6420,0xe421,0x6520,0xe521,0x6620,0xe621,0x6720,0xe721,
    0x6828,0xe829,0x6928,0xe929,0x6a28,0xea29,0x6b28,0xeb29,
    0x6c28,0xec29,0x6d28,0xed29,0x6e28,0xee29,0x6f28,0xef29,
    0x7020,0xf021,0x7120,0xf121,0x7220,0xf221,0x7320,0xf321,
    0x7420,0xf421,0x7520,0xf521,0x7620,0xf621,0x7720,0xf721,
    0x7828,0xf829,0x7928,0xf929,0x7a28,0xfa29,0x7b28,0xfb29,
    0x7c28,0xfc29,0x7d28,0xfd29,0x7e28,0xfe29,0x7f28,0xff29,
};

/* rraTable[i] = ((i >> 1) << 8) | ((i >> 1) & 0x28) | (i & 1), i = 0..255 */
static const uint16 rraTable[256] = {
    0x0000,0x0001,0x0100,0x0101,0x0200,0x0201,0x0300,0x0301,
    0x0400,0x0401,0x0500,0x0501,0x0600,0x0601,0x0700,0x0701,
    0x0808,0x0809,0x0908,0x0909,0x0a08,0x0a09,0x0b08,0x0b09,
    0x0c08,0x0c09,0x0d08,0x0d09,0x0e08,0x0e09,0x0f08,0x0f09,
    0x1000,0x1001,0x1100,0x1101,0x1200,0x1201,0x1300,0x1301,
    0x1400,0x1401,0x1500,0x1501,0x1600,0x1601,0x1700,0x1701,
    0x1808,0x1809,0x1908,0x1909,0x1a08,0x1a09,0x1b08,0x1b09,
    0x1c08,0x1c09,0x1d08,0x1d09,0x1e08,0x1e09,0x1f08,0x1f09,
    0x2020,0x2021,0x2120,0x2121,0x2220,0x2221,0x2320,0x2321,
    0x2420,0x2421,0x2520,0x2521,0x2620,0x2621,0x2720,0x2721,
    0x2828,0x2829,0x2928,0x2929,0x2a28,0x2a29,0x2b28,0x2b29,
    0x2c28,0x2c29,0x2d28,0x2d29,0x2e28,0x2e29,0x2f28,0x2f29,
    0x3020,0x3021,0x3120,0x3121,0x3220,0x3221,0x3320,0x3321,
    0x3420,0x3421,0x3520,0x3521,0x3620,0x3621,0x3720,0x3721,
    0x3828,0x3829,0x3928,0x3929,0x3a28,0x3a29,0x3b28,0x3b29,
    0x3c28,0x3c29,0x3d28,0x3d29,0x3e28,0x3e29,0x3f28,0x3f29,
    0x4000,0x4001,0x4100,0x4101,0x4200,0x4201,0x4300,0x4301,
    0x4400,0x4401,0x4500,0x4501,0x4600,0x4601,0x4700,0x4701,
    0x4808,0x4809,0x4908,0x4909,0x4a08,0x4a09,0x4b08,0x4b09,
    0x4c08,0x4c09,0x4d08,0x4d09,0x4e08,0x4e09,0x4f08,0x4f09,
    0x5000,0x5001,0x5100,0x5101,0x5200,0x5201,0x5300,0x5301,
    0x5400,0x5401,0x5500,0x5501,0x5600,0x5601,0x5700,0x5701,
    0x5808,0x5809,0x5908,0x5909,0x5a08,0x5a09,0x5b08,0x5b09,
    0x5c08,0x5c09,0x5d08,0x5d09,0x5e08,0x5e09,0x5f08,0x5f09,
    0x6020,0x6021,0x6120,0x6121,0x6220,0x6221,0x6320,0x6321,
    0x6420,0x6421,0x6520,0x6521,0x6620,0x6621,0x6720,0x6721,
    0x6828,0x6829,0x6928,0x6929,0x6a28,0x6a29,0x6b28,0x6b29,
    0x6c28,0x6c29,0x6d28,0x6d29,0x6e28,0x6e29,0x6f28,0x6f29,
    0x7020,0x7021,0x7120,0x7121,0x7220,0x7221,0x7320,0x7321,
    0x7420,0x7421,0x7520,0x7521,0x7620,0x7621,0x7720,0x7721,
    0x7828,0x7829,0x7928,0x7929,0x7a28,0x7a29,0x7b28,0x7b29,
    0x7c28,0x7c29,0x7d28,0x7d29,0x7e28,0x7e29,0x7f28,0x7f29,
};

/* addTable[i] = ((i & 0xff) << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6), i = 0..511 */
static const uint16 addTable[512] = {
    0x0040,0x0100,0x0200,0x0300,0x0400,0x0500,0x0600,0x0700,
    0x0808,0x0908,0x0a08,0x0b08,0x0c08,0x0d08,0x0e08,0x0f08,
    0x1000,0x1100,0x1200,0x1300,0x1400,0x1500,0x1600,0x1700,
    0x1808,0x1908,0x1a08,0x1b08,0x1c08,0x1d08,0x1e08,0x1f08,
    0x2020,0x2120,0x2220,0x2320,0x2420,0x2520,0x2620,0x2720,
    0x2828,0x2928,0x2a28,0x2b28,0x2c28,0x2d28,0x2e28,0x2f28,
    0x3020,0x3120,0x3220,0x3320,0x3420,0x3520,0x3620,0x3720,
    0x3828,0x3928,0x3a28,0x3b28,0x3c28,0x3d28,0x3e28,0x3f28,
    0x4000,0x4100,0x4200,0x4300,0x4400,0x4500,0x4600,0x4700,
    0x4808,0x4908,0x4a08,0x4b08,0x4c08,0x4d08,0x4e08,0x4f08,
    0x5000,0x5100,0x5200,0x5300,0x5400,0x5500,0x5600,0x5700,
    0x5808,0x5908,0x5a08,0x5b08,0x5c08,0x5d08,0x5e08,0x5f08,
    0x6020,0x6120,0x6220,0x6320,0x6420,0x6520,0x6620,0x6720,
    0x6828,0x6928,0x6a28,0x6b28,0x6c28,0x6d28,0x6e28,0x6f28,
    0x7020,0x7120,0x7220,0x7320,0x7420,0x7520,0x7620,0x7720,
    0x7828,0x7928,0x7a28,0x7b28,0x7c28,0x7d28,0x7e28,0x7f28,
    0x8080,0x8180,0x8280,0x8380,0x8480,0x8580,0x8680,0x8780,
    0x8888,0x8988,0x8a88,0x8b88,0x8c88,0x8d88,0x8e88,0x8f88,
    0x9080,0x9180,0x9280,0x9380,0x9480,0x9580,0x9680,0x9780,
    0x9888,0x9988,0x9a88,0x9b88,0x9c88,0x9d88,0x9e88,0x9f88,
    0xa0a0,0xa1a0,0xa2a0,0xa3a0,0xa4a0,0xa5a0,0xa6a0,0xa7a0,
    0xa8a8,0xa9a8,0xaaa8,0xaba8,0xaca8,0xada8,0xaea8,0xafa8,
    0xb0a0,0xb1a0,0xb2a0,0xb3a0,0xb4a0,0xb5a0,0xb6a0,0xb7a0,
    0xb8a8,0xb9a8,0xbaa8,0xbba8,0xbca8,0xbda8,0xbea8,0xbfa8,
    0xc080,0xc180,0xc280,0xc380,0xc480,0xc580,0xc680,0xc780,
    0xc888,0xc988,0xca88,0xcb88,0xcc88,0xcd88,0xce88,0xcf88,
    0xd080,0xd180,0xd280,0xd380,0xd480,0xd580,0xd680,0xd780,
    0xd888,0xd988,0xda88,0xdb88,0xdc88,0xdd88,0xde88,0xdf88,
    0xe0a0,0xe1a0,0xe2a0,0xe3a0,0xe4a0,0xe5a0,0xe6a0,0xe7a0,
    0xe8a8,0xe9a8,0xeaa8,0xeba8,0xeca8,0xeda8,0xeea8,0xefa8,
    0xf0a0,0xf1a0,0xf2a0,0xf3a0,0xf4a0,0xf5a0,0xf6a0,0xf7a0,
    0xf8a8,0xf9a8,0xfaa8,0xfba8,0xfca8,0xfda8,0xfea8,0xffa8,
    0x0040,0x0100,0x0200,0x0300,0x0400,0x0500,0x0600,0x0700,
    0x0808,0x0908,0x0a08,0x0b08,0x0c08,0x0d08,0x0e08,0x0f08,
    0x1000,0x1100,0x1200,0x1300,0x1400,0x1500,0x1600,0x1700,
    0x1808,0x1908,0x1a08,0x1b08,0x1c08,0x1d08,0x1e08,0x1f08,
    0x2020,0x2120,0x2220,0x2320,0x2420,0x2520,0x2620,0x2720,
    0x2828,0x2928,0x2a28,0x2b28,0x2c28,0x2d28,0x2e28,0x2f28,
    0x3020,0x3120,0x3220,0x3320,0x3420,0x3520,0x3620,0x3720,
    0x3828,0x3928,0x3a28,0x3b28,0x3c28,0x3d28,0x3e28,0x3f28,
    0x4000,0x4100,0x4200,0x4300,0x4400,0x4500,0x4600,0x4700,
    0x4808,0x4908,0x4a08,0x4b08,0x4c08,0x4d08,0x4e08,0x4f08,
    0x5000,0x5100,0x5200,0x5300,0x5400,0x5500,0x5600,0x5700,
    0x5808,0x5908,0x5a08,0x5b08,0x5c08,0x5d08,0x5e08,0x5f08,
    0x6020,0x6120,0x6220,0x6320,0x6420,0x6520,0x6620,0x6720,
    0x6828,0x6928,0x6a28,0x6b28,0x6c28,0x6d28,0x6e28,0x6f28,
    0x7020,0x7120,0x7220,0x7320,0x7420,0x7520,0x7620,0x7720,
    0x7828,0x7928,0x7a28,0x7b28,0x7c28,0x7d28,0x7e28,0x7f28,
    0x8080,0x8180,0x8280,0x8380,0x8480,0x8580,0x8680,0x8780,
    0x8888,0x8988,0x8a88,0x8b88,0x8c88,0x8d88,0x8e88,0x8f88,
    0x9080,0x9180,0x9280,0x9380,0x9480,0x9580,0x9680,0x9780,
    0x9888,0x9988,0x9a88,0x9b88,0x9c88,0x9d88,0x9e88,0x9f88,
    0xa0a0,0xa1a0,0xa2a0,0xa3a0,0xa4a0,0xa5a0,0xa6a0,0xa7a0,
    0xa8a8,0xa9a8,0xaaa8,0xaba8,0xaca8,0xada8,0xaea8,0xafa8,
    0xb0a0,0xb1a0,0xb2a0,0xb3a0,0xb4a0,0xb5a0,0xb6a0,0xb7a0,
    0xb8a8,0xb9a8,0xbaa8,0xbba8,0xbca8,0xbda8,0xbea8,0xbfa8,
    0xc080,0xc180,0xc280,0xc380,0xc480,0xc580,0xc680,0xc780,
    0xc888,0xc988,0xca88,0xcb88,0xcc88,0xcd88,0xce88,0xcf88,
    0xd080,0xd180,0xd280,0xd380,0xd480,0xd580,0xd680,0xd780,
    0xd888,0xd988,0xda88,0xdb88,0xdc88,0xdd88,0xde88,0xdf88,
    0xe0a0,0xe1a0,0xe2a0,0xe3a0,0xe4a0,0xe5a0,0xe6a0,0xe7a0,
    0xe8a8,0xe9a8,0xeaa8,0xeba8,0xeca8,0xeda8,0xeea8,0xefa8,
    0xf0a0,0xf1a0,0xf2a0,0xf3a0,0xf4a0,0xf5a0,0xf6a0,0xf7a0,
    0xf8a8,0xf9a8,0xfaa8,0xfba8,0xfca8,0xfda8,0xfea8,0xffa8,
};

/* subTable[i] = ((i & 0xff) << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6) | 2, i = 0..255 */
static const uint16 subTable[256] = {
    0x0042,0x0102,0x0202,0x0302,0x0402,0x0502,0x0602,0x0702,
    0x080a,0x090a,0x0a0a,0x0b0a,0x0c0a,0x0d0a,0x0e0a,0x0f0a,
    0x1002,0x1102,0x1202,0x1302,0x1402,0x1502,0x1602,0x1702,
    0x180a,0x190a,0x1a0a,0x1b0a,0x1c0a,0x1d0a,0x1e0a,0x1f0a,
    0x2022,0x2122,0x2222,0x2322,0x2422,0x2522,0x2622,0x2722,
    0x282a,0x292a,0x2a2a,0x2b2a,0x2c2a,0x2d2a,0x2e2a,0x2f2a,
    0x3022,0x3122,0x3222,0x3322,0x3422,0x3522,0x3622,0x3722,
    0x382a,0x392a,0x3a2a,0x3b2a,0x3c2a,0x3d2a,0x3e2a,0x3f2a,
    0x4002,0x4102,0x4202,0x4302,0x4402,0x4502,0x4602,0x4702,
    0x480a,0x490a,0x4a0a,0x4b0a,0x4c0a,0x4d0a,0x4e0a,0x4f0a,
    0x5002,0x5102,0x5202,0x5302,0x5402,0x5502,0x5602,0x5702,
    0x580a,0x590a,0x5a0a,0x5b0a,0x5c0a,0x5d0a,0x5e0a,0x5f0a,
    0x6022,0x6122,0x6222,0x6322,0x6422,0x6522,0x6622,0x6722,
    0x682a,0x692a,0x6a2a,0x6b2a,0x6c2a,0x6d2a,0x6e2a,0x6f2a,
    0x7022,0x7122,0x7222,0x7322,0x7422,0x7522,0x7622,0x7722,
    0x782a,0x792a,0x7a2a,0x7b2a,0x7c2a,0x7d2a,0x7e2a,0x7f2a,
    0x8082,0x8182,0x8282,0x8382,0x8482,0x8582,0x8682,0x8782,
    0x888a,0x898a,0x8a8a,0x8b8a,0x8c8a,0x8d8a,0x8e8a,0x8f8a,
    0x9082,0x9182,0x9282,0x9382,0x9482,0x9582,0x9682,0x9782,
    0x988a,0x998a,0x9a8a,0x9b8a,0x9c8a,0x9d8a,0x9e8a,0x9f8a,
    0xa0a2,0xa1a2,0xa2a2,0xa3a2,0xa4a2,0xa5a2,0xa6a2,0xa7a2,
    0xa8aa,0xa9aa,0xaaaa,0xabaa,0xacaa,0xadaa,0xaeaa,0xafaa,
    0xb0a2,0xb1a2,0xb2a2,0xb3a2,0xb4a2,0xb5a2,0xb6a2,0xb7a2,
    0xb8aa,0xb9aa,0xbaaa,0xbbaa,0xbcaa,0xbdaa,0xbeaa,0xbfaa,
    0xc082,0xc182,0xc282,0xc382,0xc482,0xc582,0xc682,0xc782,
    0xc88a,0xc98a,0xca8a,0xcb8a,0xcc8a,0xcd8a,0xce8a,0xcf8a,
    0xd082,0xd182,0xd282,0xd382,0xd482,0xd582,0xd682,0xd782,
    0xd88a,0xd98a,0xda8a,0xdb8a,0xdc8a,0xdd8a,0xde8a,0xdf8a,
    0xe0a2,0xe1a2,0xe2a2,0xe3a2,0xe4a2,0xe5a2,0xe6a2,0xe7a2,
    0xe8aa,0xe9aa,0xeaaa,0xebaa,0xecaa,0xedaa,0xeeaa,0xefaa,
    0xf0a2,0xf1a2,0xf2a2,0xf3a2,0xf4a2,0xf5a2,0xf6a2,0xf7a2,
    0xf8aa,0xf9aa,0xfaaa,0xfbaa,0xfcaa,0xfdaa,0xfeaa,0xffaa,
};

/* andTable[i] = (i << 8) | (i & 0xa8) | ((i == 0) << 6) | 0x10 | parityTable[i], i = 0..255 */
static const uint16 andTable[256] = {
    0x0054,0x0110,0x0210,0x0314,0x0410,0x0514,0x0614,0x0710,
    0x0818,0x091c,0x0a1c,0x0b18,0x0c1c,0x0d18,0x0e18,0x0f1c,
    0x1010,0x1114,0x1214,0x1310,0x1414,0x1510,0x1610,0x1714,
    0x181c,0x1918,0x1a18,0x1b1c,0x1c18,0x1d1c,0x1e1c,0x1f18,
    0x2030,0x2134,0x2234,0x2330,0x2434,0x2530,0x2630,0x2734,
    0x283c,0x2938,0x2a38,0x2b3c,0x2c38,0x2d3c,0x2e3c,0x2f38,
    0x3034,0x3130,0x3230,0x3334,0x3430,0x3534,0x3634,0x3730,
    0x3838,0x393c,0x3a3c,0x3b38,0x3c3c,0x3d38,0x3e38,0x3f3c,
    0x4010,0x4114,0x4214,0x4310,0x4414,0x4510,0x4610,0x4714,
    0x481c,0x4918,0x4a18,0x4b1c,0x4c18,0x4d1c,0x4e1c,0x4f18,
    0x5014,0x5110,0x5210,0x5314,0x5410,0x5514,0x5614,0x5710,
    0x5818,0x591c,0x5a1c,0x5b18,0x5c1c,0x5d18,0x5e18,0x5f1c,
    0x6034,0x6130,0x6230,0x6334,0x6430,0x6534,0x6634,0x6730,
    0x6838,0x693c,0x6a3c,0x6b38,0x6c3c,0x6d38,0x6e38,0x6f3c,
    0x7030,0x7134,0x7234,0x7330,0x7434,0x7530,0x7630,0x7734,
    0x783c,0x7938,0x7a38,0x7b3c,0x7c38,0x7d3c,0x7e3c,0x7f38,
    0x8090,0x8194,0x8294,0x8390,0x8494,0x8590,0x8690,0x8794,
    0x889c,0x8998,0x8a98,0x8b9c,0x8c98,0x8d9c,0x8e9c,0x8f98,
    0x9094,0x9190,0x9290,0x9394,0x9490,0x9594,0x9694,0x9790,
    0x9898,0x999c,0x9a9c,0x9b98,0x9c9c,0x9d98,0x9e98,0x9f9c,
    0xa0b4,0xa1b0,0xa2b0,0xa3b4,0xa4b0,0xa5b4,0xa6b4,0xa7b0,
    0xa8b8,0xa9bc,0xaabc,0xabb8,0xacbc,0xadb8,0xaeb8,0xafbc,
    0xb0b0,0xb1b4,0xb2b4,0xb3b0,0xb4b4,0xb5b0,0xb6b0,0xb7b4,
    0xb8bc,0xb9b8,0xbab8,0xbbbc,0xbcb8,0xbdbc,0xbebc,0xbfb8,
    0xc094,0xc190,0xc290,0xc394,0xc490,0xc594,0xc694,0xc790,
    0xc898,0xc99c,0xca9c,0xcb98,0xcc9c,0xcd98,0xce98,0xcf9c,
    0xd090,0xd194,0xd294,0xd390,0xd494,0xd590,0xd690,0xd794,
    0xd89c,0xd998,0xda98,0xdb9c,0xdc98,0xdd9c,0xde9c,0xdf98,
    0xe0b0,0xe1b4,0xe2b4,0xe3b0,0xe4b4,0xe5b0,0xe6b0,0xe7b4,
    0xe8bc,0xe9b8,0xeab8,0xebbc,0xecb8,0xedbc,0xeebc,0xefb8,
    0xf0b4,0xf1b0,0xf2b0,0xf3b4,0xf4b0,0xf5b4,0xf6b4,0xf7b0,
    0xf8b8,0xf9bc,0xfabc,0xfbb8,0xfcbc,0xfdb8,0xfeb8,0xffbc,
};

/* xororTable[i] = (i << 8) | (i & 0xa8) | ((i == 0) << 6) | parityTable[i], i = 0..255 */
static const uint16 xororTable[256] = {
    0x0044,0x0100,0x0200,0x0304,0x0400,0x0504,0x0604,0x0700,
    0x0808,0x090c,0x0a0c,0x0b08,0x0c0c,0x0d08,0x0e08,0x0f0c,
    0x1000,0x1104,0x1204,0x1300,0x1404,0x1500,0x1600,0x1704,
    0x180c,0x1908,0x1a08,0x1b0c,0x1c08,0x1d0c,0x1e0c,0x1f08,
    0x2020,0x2124,0x2224,0x2320,0x2424,0x2520,0x2620,0x2724,
    0x282c,0x2928,0x2a28,0x2b2c,0x2c28,0x2d2c,0x2e2c,0x2f28,
    0x3024,0x3120,0x3220,0x3324,0x3420,0x3524,0x3624,0x3720,
    0x3828,0x392c,0x3a2c,0x3b28,0x3c2c,0x3d28,0x3e28,0x3f2c,
    0x4000,0x4104,0x4204,0x4300,0x4404,0x4500,0x4600,0x4704,
    0x480c,0x4908,0x4a08,0x4b0c,0x4c08,0x4d0c,0x4e0c,0x4f08,
    0x5004,0x5100,0x5200,0x5304,0x5400,0x5504,0x5604,0x5700,
    0x5808,0x590c,0x5a0c,0x5b08,0x5c0c,0x5d08,0x5e08,0x5f0c,
    0x6024,0x6120,0x6220,0x6324,0x6420,0x6524,0x6624,0x6720,
    0x6828,0x692c,0x6a2c,0x6b28,0x6c2c,0x6d28,0x6e28,0x6f2c,
    0x7020,0x7124,0x7224,0x7320,0x7424,0x7520,0x7620,0x7724,
    0x782c,0x7928,0x7a28,0x7b2c,0x7c28,0x7d2c,0x7e2c,0x7f28,
    0x8080,0x8184,0x8284,0x8380,0x8484,0x8580,0x8680,0x8784,
    0x888c,0x8988,0x8a88,0x8b8c,0x8c88,0x8d8c,0x8e8c,0x8f88,
    0x9084,0x9180,0x9280,0x9384,0x9480,0x9584,0x9684,0x9780,
    0x9888,0x998c,0x9a8c,0x9b88,0x9c8c,0x9d88,0x9e88,0x9f8c,
    0xa0a4,0xa1a0,0xa2a0,0xa3a4,0xa4a0,0xa5a4,0xa6a4,0xa7a0,
    0xa8a8,0xa9ac,0xaaac,0xaba8,0xacac,0xada8,0xaea8,0xafac,
    0xb0a0,0xb1a4,0xb2a4,0xb3a0,0xb4a4,0xb5a0,0xb6a0,0xb7a4,
    0xb8ac,0xb9a8,0xbaa8,0xbbac,0xbca8,0xbdac,0xbeac,0xbfa8,
    0xc084,0xc180,0xc280,0xc384,0xc480,0xc584,0xc684,0xc780,
    0xc888,0xc98c,0xca8c,0xcb88,0xcc8c,0xcd88,0xce88,0xcf8c,
    0xd080,0xd184,0xd284,0xd380,0xd484,0xd580,0xd680,0xd784,
    0xd88c,0xd988,0xda88,0xdb8c,0xdc88,0xdd8c,0xde8c,0xdf88,
    0xe0a0,0xe1a4,0xe2a4,0xe3a0,0xe4a4,0xe5a0,0xe6a0,0xe7a4,
    0xe8ac,0xe9a8,0xeaa8,0xebac,0xeca8,0xedac,0xeeac,0xefa8,
    0xf0a4,0xf1a0,0xf2a0,0xf3a4,0xf4a0,0xf5a4,0xf6a4,0xf7a0,
    0xf8a8,0xf9ac,0xfaac,0xfba8,0xfcac,0xfda8,0xfea8,0xffac,
};

/* rotateShiftTable[i] = (i & 0xa8) | (((i & 0xff) == 0) << 6) | parityTable[i & 0xff], i = 0..255 */
static const uint8 rotateShiftTable[256] = {
     68,  0,  0,  4,  0,  4,  4,  0,  8, 12, 12,  8, 12,  8,  8, 12,
      0,  4,  4,  0,  4,  0,  0,  4, 12,  8,  8, 12,  8, 12, 12,  8,
     32, 36, 36, 32, 36, 32, 32, 36, 44, 40, 40, 44, 40, 44, 44, 40,
     36, 32, 32, 36, 32, 36, 36, 32, 40, 44, 44, 40, 44, 40, 40, 44,
      0,  4,  4,  0,  4,  0,  0,  4, 12,  8,  8, 12,  8, 12, 12,  8,
      4,  0,  0,  4,  0,  4,  4,  0,  8, 12, 12,  8, 12,  8,  8, 12,
     36, 32, 32, 36, 32, 36, 36, 32, 40, 44, 44, 40, 44, 40, 40, 44,
     32, 36, 36, 32, 36, 32, 32, 36, 44, 40, 40, 44, 40, 44, 44, 40,
    128,132,132,128,132,128,128,132,140,136,136,140,136,140,140,136,
    132,128,128,132,128,132,132,128,136,140,140,136,140,136,136,140,
    164,160,160,164,160,164,164,160,168,172,172,168,172,168,168,172,
    160,164,164,160,164,160,160,164,172,168,168,172,168,172,172,168,
    132,128,128,132,128,132,132,128,136,140,140,136,140,136,136,140,
    128,132,132,128,132,128,128,132,140,136,136,140,136,140,140,136,
    160,164,164,160,164,160,160,164,172,168,168,172,168,172,172,168,
    164,160,160,164,160,164,164,160,168,172,172,168,172,168,168,172,
};

/* incZ80Table[i] = (i & 0xa8) | (((i & 0xff) == 0) << 6) |
                        (((i & 0xf) == 0) << 4) | ((i == 0x80) << 2), i = 0..256 */
static const uint8 incZ80Table[257] = {
     80,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     16,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     48, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
     48, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
     16,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     16,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     48, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
     48, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
    148,128,128,128,128,128,128,128,136,136,136,136,136,136,136,136,
    144,128,128,128,128,128,128,128,136,136,136,136,136,136,136,136,
    176,160,160,160,160,160,160,160,168,168,168,168,168,168,168,168,
    176,160,160,160,160,160,160,160,168,168,168,168,168,168,168,168,
    144,128,128,128,128,128,128,128,136,136,136,136,136,136,136,136,
    144,128,128,128,128,128,128,128,136,136,136,136,136,136,136,136,
    176,160,160,160,160,160,160,160,168,168,168,168,168,168,168,168,
    176,160,160,160,160,160,160,160,168,168,168,168,168,168,168,168, 80,
};

/* decZ80Table[i] = (i & 0xa8) | (((i & 0xff) == 0) << 6) |
                        (((i & 0xf) == 0xf) << 4) | ((i == 0x7f) << 2) | 2, i = 0..255 */
static const uint8 decZ80Table[256] = {
     66,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 26,
      2,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 26,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 58,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 58,
      2,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 26,
      2,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 26,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 58,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 62,
    130,130,130,130,130,130,130,130,138,138,138,138,138,138,138,154,
    130,130,130,130,130,130,130,130,138,138,138,138,138,138,138,154,
    162,162,162,162,162,162,162,162,170,170,170,170,170,170,170,186,
    162,162,162,162,162,162,162,162,170,170,170,170,170,170,170,186,
    130,130,130,130,130,130,130,130,138,138,138,138,138,138,138,154,
    130,130,130,130,130,130,130,130,138,138,138,138,138,138,138,154,
    162,162,162,162,162,162,162,162,170,170,170,170,170,170,170,186,
    162,162,162,162,162,162,162,162,170,170,170,170,170,170,170,186,
};

/* cbitsZ80Table[i] = (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) | ((i >> 8) & 1), i = 0..511 */
static const uint8 cbitsZ80Table[512] = {
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
};

/* cbitsZ80DupTable[i] = (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) |
                            ((i >> 8) & 1) | (i & 0xa8), i = 0..511 */
static const uint8 cbitsZ80DupTable[512] = {
      0,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     16, 16, 16, 16, 16, 16, 16, 16, 24, 24, 24, 24, 24, 24, 24, 24,
     32, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
     48, 48, 48, 48, 48, 48, 48, 48, 56, 56, 56, 56, 56, 56, 56, 56,
      0,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8,  8,  8,  8,  8,
     16, 16, 16, 16, 16, 16, 16, 16, 24, 24, 24, 24, 24, 24, 24, 24,
     32, 32, 32, 32, 32, 32, 32, 32, 40, 40, 40, 40, 40, 40, 40, 40,
     48, 48, 48, 48, 48, 48, 48, 48, 56, 56, 56, 56, 56, 56, 56, 56,
    132,132,132,132,132,132,132,132,140,140,140,140,140,140,140,140,
    148,148,148,148,148,148,148,148,156,156,156,156,156,156,156,156,
    164,164,164,164,164,164,164,164,172,172,172,172,172,172,172,172,
    180,180,180,180,180,180,180,180,188,188,188,188,188,188,188,188,
    132,132,132,132,132,132,132,132,140,140,140,140,140,140,140,140,
    148,148,148,148,148,148,148,148,156,156,156,156,156,156,156,156,
    164,164,164,164,164,164,164,164,172,172,172,172,172,172,172,172,
    180,180,180,180,180,180,180,180,188,188,188,188,188,188,188,188,
      5,  5,  5,  5,  5,  5,  5,  5, 13, 13, 13, 13, 13, 13, 13, 13,
     21, 21, 21, 21, 21, 21, 21, 21, 29, 29, 29, 29, 29, 29, 29, 29,
     37, 37, 37, 37, 37, 37, 37, 37, 45, 45, 45, 45, 45, 45, 45, 45,
     53, 53, 53, 53, 53, 53, 53, 53, 61, 61, 61, 61, 61, 61, 61, 61,
      5,  5,  5,  5,  5,  5,  5,  5, 13, 13, 13, 13, 13, 13, 13, 13,
     21, 21, 21, 21, 21, 21, 21, 21, 29, 29, 29, 29, 29, 29, 29, 29,
     37, 37, 37, 37, 37, 37, 37, 37, 45, 45, 45, 45, 45, 45, 45, 45,
     53, 53, 53, 53, 53, 53, 53, 53, 61, 61, 61, 61, 61, 61, 61, 61,
    129,129,129,129,129,129,129,129,137,137,137,137,137,137,137,137,
    145,145,145,145,145,145,145,145,153,153,153,153,153,153,153,153,
    161,161,161,161,161,161,161,161,169,169,169,169,169,169,169,169,
    177,177,177,177,177,177,177,177,185,185,185,185,185,185,185,185,
    129,129,129,129,129,129,129,129,137,137,137,137,137,137,137,137,
    145,145,145,145,145,145,145,145,153,153,153,153,153,153,153,153,
    161,161,161,161,161,161,161,161,169,169,169,169,169,169,169,169,
    177,177,177,177,177,177,177,177,185,185,185,185,185,185,185,185,
};

/* cbits2Z80Table[i] = (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) | ((i >> 8) & 1) | 2, i = 0..511 */
static const uint8 cbits2Z80Table[512] = {
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
};

/* cbits2Z80DupTable[i] = (i & 0x10) | (((i >> 6) ^ (i >> 5)) & 4) | ((i >> 8) & 1) | 2 |
                            (i & 0xa8), i = 0..511 */
static const uint8 cbits2Z80DupTable[512] = {
      2,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 10,
     18, 18, 18, 18, 18, 18, 18, 18, 26, 26, 26, 26, 26, 26, 26, 26,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 42,
     50, 50, 50, 50, 50, 50, 50, 50, 58, 58, 58, 58, 58, 58, 58, 58,
      2,  2,  2,  2,  2,  2,  2,  2, 10, 10, 10, 10, 10, 10, 10, 10,
     18, 18, 18, 18, 18, 18, 18, 18, 26, 26, 26, 26, 26, 26, 26, 26,
     34, 34, 34, 34, 34, 34, 34, 34, 42, 42, 42, 42, 42, 42, 42, 42,
     50, 50, 50, 50, 50, 50, 50, 50, 58, 58, 58, 58, 58, 58, 58, 58,
    134,134,134,134,134,134,134,134,142,142,142,142,142,142,142,142,
    150,150,150,150,150,150,150,150,158,158,158,158,158,158,158,158,
    166,166,166,166,166,166,166,166,174,174,174,174,174,174,174,174,
    182,182,182,182,182,182,182,182,190,190,190,190,190,190,190,190,
    134,134,134,134,134,134,134,134,142,142,142,142,142,142,142,142,
    150,150,150,150,150,150,150,150,158,158,158,158,158,158,158,158,
    166,166,166,166,166,166,166,166,174,174,174,174,174,174,174,174,
    182,182,182,182,182,182,182,182,190,190,190,190,190,190,190,190,
      7,  7,  7,  7,  7,  7,  7,  7, 15, 15, 15, 15, 15, 15, 15, 15,
     23, 23, 23, 23, 23, 23, 23, 23, 31, 31, 31, 31, 31, 31, 31, 31,
     39, 39, 39, 39, 39, 39, 39, 39, 47, 47, 47, 47, 47, 47, 47, 47,
     55, 55, 55, 55, 55, 55, 55, 55, 63, 63, 63, 63, 63, 63, 63, 63,
      7,  7,  7,  7,  7,  7,  7,  7, 15, 15, 15, 15, 15, 15, 15, 15,
     23, 23, 23, 23, 23, 23, 23, 23, 31, 31, 31, 31, 31, 31, 31, 31,
     39, 39, 39, 39, 39, 39, 39, 39, 47, 47, 47, 47, 47, 47, 47, 47,
     55, 55, 55, 55, 55, 55, 55, 55, 63, 63, 63, 63, 63, 63, 63, 63,
    131,131,131,131,131,131,131,131,139,139,139,139,139,139,139,139,
    147,147,147,147,147,147,147,147,155,155,155,155,155,155,155,155,
    163,163,163,163,163,163,163,163,171,171,171,171,171,171,171,171,
    179,179,179,179,179,179,179,179,187,187,187,187,187,187,187,187,
    131,131,131,131,131,131,131,131,139,139,139,139,139,139,139,139,
    147,147,147,147,147,147,147,147,155,155,155,155,155,155,155,155,
    163,163,163,163,163,163,163,163,171,171,171,171,171,171,171,171,
    179,179,179,179,179,179,179,179,187,187,187,187,187,187,187,187,
};

/* negTable[i] = (((i & 0x0f) != 0) << 4) | ((i == 0x80) << 2) | 2 | (i != 0), i = 0..255 */
static const uint8 negTable[256] = {
     2,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     7,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
     3,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
};

/* rrdrldTable[i] = (i << 8) | (i & 0xa8) | (((i & 0xff) == 0) << 6) | parityTable[i], i = 0..255 */
static const uint16 rrdrldTable[256] = {
    0x0044,0x0100,0x0200,0x0304,0x0400,0x0504,0x0604,0x0700,
    0x0808,0x090c,0x0a0c,0x0b08,0x0c0c,0x0d08,0x0e08,0x0f0c,
    0x1000,0x1104,0x1204,0x1300,0x1404,0x1500,0x1600,0x1704,
    0x180c,0x1908,0x1a08,0x1b0c,0x1c08,0x1d0c,0x1e0c,0x1f08,
    0x2020,0x2124,0x2224,0x2320,0x2424,0x2520,0x2620,0x2724,
    0x282c,0x2928,0x2a28,0x2b2c,0x2c28,0x2d2c,0x2e2c,0x2f28,
    0x3024,0x3120,0x3220,0x3324,0x3420,0x3524,0x3624,0x3720,
    0x3828,0x392c,0x3a2c,0x3b28,0x3c2c,0x3d28,0x3e28,0x3f2c,
    0x4000,0x4104,0x4204,0x4300,0x4404,0x4500,0x4600,0x4704,
    0x480c,0x4908,0x4a08,0x4b0c,0x4c08,0x4d0c,0x4e0c,0x4f08,
    0x5004,0x5100,0x5200,0x5304,0x5400,0x5504,0x5604,0x5700,
    0x5808,0x590c,0x5a0c,0x5b08,0x5c0c,0x5d08,0x5e08,0x5f0c,
    0x6024,0x6120,0x6220,0x6324,0x6420,0x6524,0x6624,0x6720,
    0x6828,0x692c,0x6a2c,0x6b28,0x6c2c,0x6d28,0x6e28,0x6f2c,
    0x7020,0x7124,0x7224,0x7320,0x7424,0x7520,0x7620,0x7724,
    0x782c,0x7928,0x7a28,0x7b2c,0x7c28,0x7d2c,0x7e2c,0x7f28,
    0x8080,0x8184,0x8284,0x8380,0x8484,0x8580,0x8680,0x8784,
    0x888c,0x8988,0x8a88,0x8b8c,0x8c88,0x8d8c,0x8e8c,0x8f88,
    0x9084,0x9180,0x9280,0x9384,0x9480,0x9584,0x9684,0x9780,
    0x9888,0x998c,0x9a8c,0x9b88,0x9c8c,0x9d88,0x9e88,0x9f8c,
    0xa0a4,0xa1a0,0xa2a0,0xa3a4,0xa4a0,0xa5a4,0xa6a4,0xa7a0,
    0xa8a8,0xa9ac,0xaaac,0xaba8,0xacac,0xada8,0xaea8,0xafac,
    0xb0a0,0xb1a4,0xb2a4,0xb3a0,0xb4a4,0xb5a0,0xb6a0,0xb7a4,
    0xb8ac,0xb9a8,0xbaa8,0xbbac,0xbca8,0xbdac,0xbeac,0xbfa8,
    0xc084,0xc180,0xc280,0xc384,0xc480,0xc584,0xc684,0xc780,
    0xc888,0xc98c,0xca8c,0xcb88,0xcc8c,0xcd88,0xce88,0xcf8c,
    0xd080,0xd184,0xd284,0xd380,0xd484,0xd580,0xd680,0xd784,
    0xd88c,0xd988,0xda88,0xdb8c,0xdc88,0xdd8c,0xde8c,0xdf88,
    0xe0a0,0xe1a4,0xe2a4,0xe3a0,0xe4a4,0xe5a0,0xe6a0,0xe7a4,
    0xe8ac,0xe9a8,0xeaa8,0xebac,0xeca8,0xedac,0xeeac,0xefa8,
    0xf0a4,0xf1a0,0xf2a0,0xf3a4,0xf4a0,0xf5a4,0xf6a4,0xf7a0,
    0xf8a8,0xf9ac,0xfaac,0xfba8,0xfcac,0xfda8,0xfea8,0xffac,
};

/* cpTable[i] = (i & 0x80) | (((i & 0xff) == 0) << 6), i = 0..255 */
static const uint8 cpTable[256] = {
     64,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
};

/* remove comments to generate table contents and add a call to
 altairz80_print_tables in the altairz80_init
static void altairz80_print_tables(void) {
*/
/* parityTable */
/*
    uint32 i, v;
    for (i = 0; i < 256; i++) {
        v =     ((i & 1)        + ((i & 2) >> 1)    + ((i & 4) >> 2)    + ((i & 8) >> 3) +
                ((i & 16) >> 4) + ((i & 32) >> 5)   + ((i & 64) >> 6)   + ((i & 128) >> 7)) % 2 ? 0 : 4;
        printf("%1d,", v);
        if ( ((i+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* incTable */
/*
    uint32 temp, v;
    for (temp = 0; temp <= 256; temp++) {
        v = (temp & 0xa8) | (((temp & 0xff) == 0) << 6) | (((temp & 0xf) == 0) << 4);
        printf("%3d,", v);
        if ( ((temp+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* decTable */
/*
    uint32 temp, v;
    for (temp = 0; temp < 256; temp++) {
        v = (temp & 0xa8) | (((temp & 0xff) == 0) << 6) | (((temp & 0xf) == 0xf) << 4) | 2;
        printf("%3d,", v);
        if ( ((temp+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* cbitsTable */
/*
    uint32 cbits, v;
    for (cbits = 0; cbits < 512; cbits++) {
        v = (cbits & 0x10) | ((cbits >> 8) & 1);
        printf("%2d,", v);
        if ( ((cbits+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* cbitsDup8Table */
/*
    uint32 cbits, v;
    for (cbits = 0; cbits < 512; cbits++) {
        v = (cbits & 0x10) | ((cbits >> 8) & 1) | ((cbits & 0xff) << 8) | (cbits & 0xa8) | (((cbits & 0xff) == 0) << 6);
        printf("0x%04x,", v);
        if ( ((cbits+1) & 0x7) == 0) {
            printf("\n");
        }
    }
*/
/* cbitsDup16Table */
/*
    uint32 cbits, v;
    for (cbits = 0; cbits < 512; cbits++) {
        v = (cbits & 0x10) | ((cbits >> 8) & 1) | (cbits & 0x28);
        printf("%2d,", v);
        if ( ((cbits+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* cbits2Table */
/*
    uint32 cbits, v;
    for (cbits = 0; cbits < 512; cbits++) {
        v = (cbits & 0x10) | ((cbits >> 8) & 1) | 2;
        printf("%2d,", v);
        if ( ((cbits+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* rrcaTable */
/*
    uint32 temp, sum, v;
    for (temp = 0; temp < 256; temp++) {
        sum = temp >> 1;
        v = ((temp & 1) << 15) | (sum << 8) | (sum & 0x28) | (temp & 1);
        printf("0x%04x,", v);
        if ( ((temp+1) & 0x7) == 0) {
            printf("\n");
        }
    }
*/
/* rraTable */
/*
    uint32 temp, sum, v;
    for (temp = 0; temp < 256; temp++) {
        sum = temp >> 1;
        v = (sum << 8) | (sum & 0x28) | (temp & 1);
        printf("0x%04x,", v);
        if ( ((temp+1) & 0x7) == 0) {
            printf("\n");
        }
    }
*/
/* addTable */
/*
    uint32 sum, v;
    for (sum = 0; sum < 512; sum++) {
        v = ((sum & 0xff) << 8) | (sum & 0xa8) | (((sum & 0xff) == 0) << 6);
        printf("0x%04x,", v);
        if ( ((sum+1) & 0x7) == 0) {
            printf("\n");
        }
    }
*/
/* subTable */
/*
    uint32 sum, v;
    for (sum = 0; sum < 256; sum++) {
        v = ((sum & 0xff) << 8) | (sum & 0xa8) | (((sum & 0xff) == 0) << 6) | 2;
        printf("0x%04x,", v);
        if ( ((sum+1) & 0x7) == 0) {
            printf("\n");
        }
    }
*/
/* andTable */
/*
    uint32 sum, v;
    for (sum = 0; sum < 256; sum++) {
        v = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | 0x10 | parityTable[sum];
        printf("0x%04x,", v);
        if ( ((sum+1) & 0x7) == 0) {
            printf("\n");
        }
    }
*/
/* xororTable */
/*
    uint32 sum, v;
    for (sum = 0; sum < 256; sum++) {
        v = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parityTable[sum];
        printf("0x%04x,", v);
        if ( ((sum+1) & 0x7) == 0) {
            printf("\n");
        }
    }
*/
/* rotateShiftTable */
/*
    uint32 temp, v;
    for (temp = 0; temp < 256; temp++) {
        v = (temp & 0xa8) | (((temp & 0xff) == 0) << 6) | PARITY(temp);
        printf("%3d,", v);
        if ( ((temp+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* incZ80Table */
/*
    uint32 temp, v;
    for (temp = 0; temp < 256; temp++) {
        v = (temp & 0xa8) | (((temp & 0xff) == 0) << 6) |
            (((temp & 0xf) == 0) << 4) | ((temp == 0x80) << 2);
        printf("%3d,", v);
        if ( ((temp+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* decZ80Table */
/*
    uint32 temp, v;
    for (temp = 0; temp < 256; temp++) {
        v = (temp & 0xa8) | (((temp & 0xff) == 0) << 6) |
            (((temp & 0xf) == 0xf) << 4) | ((temp == 0x7f) << 2) | 2;
        printf("%3d,", v);
        if ( ((temp+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* cbitsZ80Table */
/*
    uint32 cbits, v;
    for (cbits = 0; cbits < 512; cbits++) {
        v = (cbits & 0x10) | (((cbits >> 6) ^ (cbits >> 5)) & 4) |
            ((cbits >> 8) & 1);
        printf("%2d,", v);
        if ( ((cbits+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* cbitsZ80DupTable */
/*
    uint32 cbits, v;
    for (cbits = 0; cbits < 512; cbits++) {
        v = (cbits & 0x10) | (((cbits >> 6) ^ (cbits >> 5)) & 4) |
            ((cbits >> 8) & 1) | (cbits & 0xa8);
        printf("%3d,", v);
        if ( ((cbits+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* cbits2Z80Table */
/*
    uint32 cbits, v;
    for (cbits = 0; cbits < 512; cbits++) {
        v = (((cbits >> 6) ^ (cbits >> 5)) & 4) | (cbits & 0x10) | 2 | ((cbits >> 8) & 1);
        printf("%2d,", v);
        if ( ((cbits+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* cbits2Z80DupTable */
/*
    uint32 cbits, v;
    for (cbits = 0; cbits < 512; cbits++) {
        v = (((cbits >> 6) ^ (cbits >> 5)) & 4) | (cbits & 0x10) | 2 | ((cbits >> 8) & 1) |
            (cbits & 0xa8);
        printf("%3d,", v);
        if ( ((cbits+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* negTable */
/*
    uint32 temp, v;
    for (temp = 0; temp < 256; temp++) {
        v = (((temp & 0x0f) != 0) << 4) | ((temp == 0x80) << 2) | 2 | (temp != 0);
        printf("%2d,", v);
        if ( ((temp+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* rrdrldTable */
/*
    uint32 acu, v;
    for (acu = 0; acu < 256; acu++) {
        v = (acu << 8) | (acu & 0xa8) | (((acu & 0xff) == 0) << 6) | parityTable[acu];
        printf("0x%04x,", v);
        if ( ((acu+1) & 0x7) == 0) {
            printf("\n");
        }
    }
*/
/* cpTable */
/*
    uint32 sum, v;
    for (sum = 0; sum < 256; sum++) {
        v = (sum & 0x80) | (((sum & 0xff) == 0) << 6);
        printf("%3d,", v);
        if ( ((sum+1) & 0xf) == 0) {
            printf("\n");
        }
    }
*/
/* remove comments to generate table contents
}
*/

/* Memory management    */

#define LOG2PAGESIZE        8
#define PAGESIZE            (1 << LOG2PAGESIZE)

static uint8 M[MAXMEMORY];   /* RAM which is present */

struct mdev { /* Structure to describe a 2^LOG2PAGESIZE byte page of address space */
    /* There are four cases
    isRAM   isEmpty     routine     code
    TRUE    FALSE       NULL        W       page is random access memory (RAM)
    FALSE   TRUE        NULL        U       no memory at this location
    FALSE   FALSE       NULL        R       page is read only memory (ROM)
    FALSE   FALSE       not NULL    M       page is mapped to memory mapped I/O routine
    other combinations are undefined!
    */
    uint32 isRAM;
    uint32 isEmpty;
    int32 (*routine)(const int32, const int32, const int32);
};

typedef struct mdev MDEV;

static MDEV ROM_PAGE    =   {FALSE, FALSE,  NULL};  /* this makes a page ROM        */
static MDEV RAM_PAGE    =   {TRUE,  FALSE,  NULL};  /* this makes a page RAM        */
static MDEV EMPTY_PAGE  =   {FALSE, TRUE,   NULL};  /* this is non-existing memory  */
static MDEV mmu_table[MAXMEMORY >> LOG2PAGESIZE];

/* Memory and I/O Resource Mapping and Unmapping routine. */
uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap) {
    uint32 page, i, addr;
    if (resource_type == RESOURCE_TYPE_MEMORY) {
        for (i = 0; i < (size >> LOG2PAGESIZE); i++) {
            addr = (baseaddr & 0xfff00) + (i << LOG2PAGESIZE);
            if ((cpu_unit.flags & UNIT_CPU_BANKED) && (addr < common))
                addr |= bankSelect << MAXBANKSIZELOG2;
            page = addr >> LOG2PAGESIZE;
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                printf("%s memory 0x%05x, handler=%p\n", unmap ? "Unmapping" : "  Mapping",
                    addr, routine);
            if (unmap) {
                if (mmu_table[page].routine == routine) {   /* unmap only if it was mapped */
                    if (MEMORYSIZE < MAXBANKSIZE)
                        if (addr < MEMORYSIZE)
                            mmu_table[page] = RAM_PAGE;
                        else
                            mmu_table[page] = EMPTY_PAGE;
                    else
                        mmu_table[page] = RAM_PAGE;
                }
            }
            else {
                mmu_table[page] = ROM_PAGE;
                mmu_table[page].routine = routine;
            }
        }
    } else if (resource_type == RESOURCE_TYPE_IO) {
        for (i = baseaddr; i < baseaddr + size; i++)
            if (unmap) {
                if (dev_table[i & 0xff].routine == routine) {
                    if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                        printf("Unmapping  IO %04x, handler=%p\n", i, routine);
                    dev_table[i & 0xff].routine = &nulldev;
                }
            }
            else {
                if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                    printf("  Mapping  IO %04x, handler=%p\n", i, routine);
                dev_table[i & 0xff].routine = routine;
            }
    } else {
        printf("%s: cannot map unknown resource type %d\n", __FUNCTION__, resource_type);
        return -1;
    }
    return 0;
}

static void PutBYTE(register uint32 Addr, const register uint32 Value) {
    MDEV m;

    Addr &= ADDRMASK;   /* registers are NOT guaranteed to be always 16-bit values */
    if ((cpu_unit.flags & UNIT_CPU_BANKED) && (Addr < common))
        Addr |= bankSelect << MAXBANKSIZELOG2;
    m = mmu_table[Addr >> LOG2PAGESIZE];

    if (m.isRAM)
        M[Addr] = Value;
    else if (m.routine)
        m.routine(Addr, 1, Value);
    else if (cpu_unit.flags & UNIT_CPU_VERBOSE) {
        if (m.isEmpty)
            printf("CPU: " ADDRESS_FORMAT " Attempt to write to non existing memory " ADDRESS_FORMAT "." NLP, PCX, Addr);
        else
            printf("CPU: " ADDRESS_FORMAT " Attempt to write to ROM " ADDRESS_FORMAT "." NLP, PCX, Addr);
    }
}

void PutBYTEExtended(register uint32 Addr, const register uint32 Value) {
    MDEV m;

    Addr &= ADDRMASKEXTENDED;
    m = mmu_table[Addr >> LOG2PAGESIZE];

    if (m.isRAM)
        M[Addr] = Value;
    else if (m.routine)
        m.routine(Addr, 1, Value);
    else if (cpu_unit.flags & UNIT_CPU_VERBOSE) {
        if (m.isEmpty)
            printf("CPU: " ADDRESS_FORMAT " Attempt to write to non existing memory " ADDRESS_FORMAT "." NLP, PCX, Addr);
        else
            printf("CPU: " ADDRESS_FORMAT " Attempt to write to ROM " ADDRESS_FORMAT "." NLP, PCX, Addr);
    }
}

static void PutWORD(register uint32 Addr, const register uint32 Value) {
    PutBYTE(Addr, Value);
    PutBYTE(Addr + 1, Value >> 8);
}

static uint32 GetBYTE(register uint32 Addr) {
    MDEV m;

    Addr &= ADDRMASK;   /* registers are NOT guaranteed to be always 16-bit values */
    if ((cpu_unit.flags & UNIT_CPU_BANKED) && (Addr < common))
        Addr |= bankSelect << MAXBANKSIZELOG2;
    m = mmu_table[Addr >> LOG2PAGESIZE];

    if (m.isRAM)
        return M[Addr]; /* RAM */
    if (m.routine)
        return m.routine(Addr, 0, 0); /* memory mapped I/O */
    if (m.isEmpty) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            printf("CPU: " ADDRESS_FORMAT " Attempt to read from non existing memory " ADDRESS_FORMAT "." NLP, PCX, Addr);
        return 0xff;
    }
    return M[Addr]; /* ROM */
}

uint32 GetBYTEExtended(register uint32 Addr) {
    MDEV m;

    Addr &= ADDRMASKEXTENDED;
    m = mmu_table[Addr >> LOG2PAGESIZE];

    if (m.isRAM)
        return M[Addr];
    if (m.routine)
        return m.routine(Addr, 0, 0);
    if (m.isEmpty) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            printf("CPU: " ADDRESS_FORMAT " Attempt to read from non existing memory " ADDRESS_FORMAT "." NLP, PCX, Addr);
        return 0xff;
    }
    return M[Addr];
}

int32 getBankSelect(void) {
    return bankSelect;
}

void setBankSelect(const int32 b) {
    bankSelect = b;
}

uint32 getCommon(void) {
    return common;
}

/* memory access during a simulation */
uint8 GetBYTEWrapper(const uint32 Addr) {
    if (chiptype == CHIP_TYPE_8086)
        return GetBYTEExtended(Addr);
    else if (cpu_unit.flags & UNIT_CPU_MMU)
        return GetBYTE(Addr);
    else
        return MOPT[Addr & ADDRMASK];
}

/* memory access during a simulation */
void PutBYTEWrapper(const uint32 Addr, const uint32 Value) {
    if (chiptype == CHIP_TYPE_8086)
        PutBYTEExtended(Addr, Value);
    else if (cpu_unit.flags & UNIT_CPU_MMU)
        PutBYTE(Addr, Value);
    else
        MOPT[Addr & ADDRMASK] = Value & 0xff;
}

/* DMA memory access during a simulation, suggested by Tony Nicholson */
uint8 GetByteDMA(const uint32 Addr) {
    if ((chiptype == CHIP_TYPE_8086) || (cpu_unit.flags & UNIT_CPU_MMU))
        return GetBYTEExtended(Addr);
    else
        return MOPT[Addr & ADDRMASK];
}

void PutByteDMA(const uint32 Addr, const uint32 Value) {
    if ((chiptype == CHIP_TYPE_8086) || (cpu_unit.flags & UNIT_CPU_MMU))
        PutBYTEExtended(Addr, Value);
    else
        MOPT[Addr & ADDRMASK] = Value & 0xff;
}

#define RAM_PP(Addr) GetBYTE(Addr++)
#define RAM_MM(Addr) GetBYTE(Addr--)
#define GET_WORD(Addr) (GetBYTE(Addr) | (GetBYTE(Addr + 1) << 8))
#define PUT_BYTE_PP(a,v) PutBYTE(a++, v)
#define PUT_BYTE_MM(a,v) PutBYTE(a--, v)
#define MM_PUT_BYTE(a,v) PutBYTE(--a, v)

#define MASK_BRK (TRUE + 1)

/* this is a modified version of sim_brk_test with two differences:
    1) is does not set sim_brk_pend to FALSE (this is left to the instruction decode)
    2) it returns MASK_BRK if a breakpoint is found but should be ignored
*/
static int32 sim_brk_lookup (const t_addr loc, const int32 btyp) {
    extern t_bool sim_brk_pend[SIM_BKPT_N_SPC];
    extern t_addr sim_brk_ploc[SIM_BKPT_N_SPC];
    extern int32 sim_do_depth;
    extern char *sim_brk_act[];
    BRKTAB *bp;
    if ((bp = sim_brk_fnd (loc)) &&                         /* entry in table?  */
        (btyp & bp -> typ) &&                               /* type match?      */
        (!sim_brk_pend[0] || (loc != sim_brk_ploc[0])) &&   /* new location?    */
        (--(bp -> cnt) <= 0)) {                             /* count reach 0?   */
        bp -> cnt = 0;                                      /* reset count      */
        sim_brk_ploc[0] = loc;                              /* save location    */
        sim_brk_act[sim_do_depth] = bp -> act;              /* set up actions   */
        sim_brk_pend[0] = TRUE;                             /* don't do twice   */
        return TRUE;
    }
    return (sim_brk_pend[0] && (loc == sim_brk_ploc[0])) ? MASK_BRK : FALSE;
}

#define PUSH(x) {                                                   \
    MM_PUT_BYTE(SP, (x) >> 8);                                      \
    MM_PUT_BYTE(SP, x);                                             \
}

#define CHECK_BREAK_BYTE(a)                                         \
    if (sim_brk_summ) {                                             \
        if (sim_brk_test((a) & 0xffff, SWMASK('M'))) {              \
            reason = STOP_MEM;                                      \
            prepareMemoryAccessMessage((a) & 0xffff);               \
            goto end_decode;                                        \
        }                                                           \
        sim_brk_pend[0] = FALSE;                                    \
    }

#define CHECK_BREAK_TWO_BYTES_EXTENDED(a1, a2, iCode)               \
    if (sim_brk_summ) {                                             \
        int32 brl = sim_brk_lookup((a1) & 0xffff, SWMASK('M'));     \
        if ((brl == TRUE) || (brl == FALSE) && (sim_brk_lookup((a2) \
                & 0xffff, SWMASK('M')) == TRUE)) {                  \
            reason = STOP_MEM;                                      \
            prepareMemoryAccessMessage((brl ? (a1):(a2)) & 0xffff); \
            iCode;                                                  \
            goto end_decode;                                        \
        }                                                           \
        sim_brk_pend[0] = FALSE;                                    \
    }

#define CHECK_BREAK_TWO_BYTES(a1, a2) CHECK_BREAK_TWO_BYTES_EXTENDED(a1, a2,;)

#define CHECK_BREAK_WORD(a) CHECK_BREAK_TWO_BYTES(a, (a + 1))

#define HALTINSTRUCTION 0x76

/*  Macros for the IN/OUT instructions INI/INIR/IND/INDR/OUTI/OTIR/OUTD/OTDR

    Pre condition
        temp == value of register B at entry of the instruction
        acu == value of transferred byte (IN or OUT)
    Post condition
        F is set correctly

    Use INOUTFLAGS_ZERO(x) for INIR/INDR/OTIR/OTDR where
        x == (C + 1) & 0xff for INIR
        x == L              for OTIR and OTDR
        x == (C - 1) & 0xff for INDR
    Use INOUTFLAGS_NONZERO(x) for INI/IND/OUTI/OUTD where
        x == (C + 1) & 0xff for INI
        x == L              for OUTI and OUTD
        x == (C - 1) & 0xff for IND
*/
#define INOUTFLAGS(syxz, x)                                             \
    AF = (AF & 0xff00) | (syxz) |               /* SF, YF, XF, ZF   */  \
        ((acu & 0x80) >> 6) |                           /* NF       */  \
        ((acu + (x)) > 0xff ? (FLAG_C | FLAG_H) : 0) |  /* CF, HF   */  \
        parityTable[((acu + (x)) & 7) ^ temp]           /* PF       */

#define INOUTFLAGS_ZERO(x)      INOUTFLAGS(FLAG_Z, x)
#define INOUTFLAGS_NONZERO(x)                                           \
    INOUTFLAGS((HIGH_REGISTER(BC) & 0xa8) | ((HIGH_REGISTER(BC) == 0) << 6), x)

int32 switch_cpu_now = TRUE; /* hharte */

t_stat sim_instr (void) {
    uint32 i;
    t_stat result;
    if ((chiptype == CHIP_TYPE_8086) || (cpu_unit.flags & UNIT_CPU_MMU))
        do {
            result = (chiptype == CHIP_TYPE_8086) ? sim_instr_8086() : sim_instr_mmu();
        } while (switch_cpu_now == FALSE);
    else {
        for (i = 0; i < MAXBANKSIZE; i++)
            MOPT[i] = M[i];
        result = sim_instr_nommu();
        for (i = 0; i < MAXBANKSIZE; i++)
            M[i] = MOPT[i];
    }
    return result;
}

static int32 clockHasChanged = FALSE;

uint32 getClockFrequency(void) {
    return clockFrequency;
}

void setClockFrequency(const uint32 Value) {
    clockFrequency = Value;
    clockHasChanged = TRUE;
}

static t_stat sim_instr_mmu (void) {
    extern t_bool sim_brk_pend[SIM_BKPT_N_SPC];
    extern int32 timerInterrupt;
    extern int32 timerInterruptHandler;
    extern int32 keyboardInterrupt;
    extern uint32 keyboardInterruptHandler;
    extern const t_bool rtc_avail;
    int32 reason = SCPE_OK;
    register uint32 specialProcessing;
    register uint32 AF;
    register uint32 BC;
    register uint32 DE;
    register uint32 HL;
    register uint32 PC;
    register uint32 SP;
    register uint32 IX;
    register uint32 IY;
    register uint32 temp = 0;
    register uint32 acu = 0;
    register uint32 sum;
    register uint32 cbits;
    register uint32 op;
    register uint32 adr;
    /*  tStates contains the number of t-states executed. One t-state is executed
        in one microsecond on a 1MHz CPU. tStates is used for real-time simulations.    */
    register uint32 tStates;
    uint32 tStatesInSlice; /* number of t-states in 10 mSec time-slice */
    uint32 startTime, now;
    int32 tStateModifier = FALSE;

    switch_cpu_now = TRUE; /* hharte */

    AF = AF_S;
    BC = BC_S;
    DE = DE_S;
    HL = HL_S;
    PC = PC_S & ADDRMASK;
    SP = SP_S;
    IX = IX_S;
    IY = IY_S;
    specialProcessing = clockFrequency | timerInterrupt | keyboardInterrupt | sim_brk_summ;
    tStates = 0;
    if (rtc_avail) {
        startTime = sim_os_msec();
        tStatesInSlice = sliceLength*clockFrequency;
    }
    else /* make sure that sim_os_msec() is not called later */
        clockFrequency = startTime = tStatesInSlice = 0;

    /* main instruction fetch/decode loop */
    while (switch_cpu_now == TRUE) {        /* loop until halted    */
        if (sim_interval <= 0) {            /* check clock queue    */
#if !UNIX_PLATFORM
            if ((reason = sim_poll_kbd()) == SCPE_STOP)   /* poll on platforms without reliable signalling */
                break;
#endif
            if ((reason = sim_process_event()))
                break;
            if (clockHasChanged) {
                clockHasChanged = FALSE;
                tStates = 0;
                if (rtc_avail) {
                    startTime = sim_os_msec();
                    tStatesInSlice = sliceLength*clockFrequency;
                }
                else /* make sure that sim_os_msec() is not called later */
                    clockFrequency = startTime = tStatesInSlice = 0;
            }
            specialProcessing = clockFrequency | timerInterrupt | keyboardInterrupt | sim_brk_summ;
        }

        if (specialProcessing) { /* quick check for special processing */
            if (clockFrequency && (tStates >= tStatesInSlice)) {
                /* clockFrequency != 0 implies that real time clock is available */
                startTime += sliceLength;
                tStates -= tStatesInSlice;
                if (startTime > (now = sim_os_msec())) {
#if defined (_WIN32)
                    Sleep(startTime - now);
#else
                    usleep(1000 * (startTime - now));
#endif
                }
            }

            if (timerInterrupt && (IFF_S & 1)) {
                timerInterrupt = FALSE;
                specialProcessing = clockFrequency | sim_brk_summ;
                IFF_S = 0; /* disable interrupts */
                CHECK_BREAK_TWO_BYTES_EXTENDED(SP - 2, SP - 1, (timerInterrupt = TRUE, IFF_S |= 1));
                if ((GetBYTE(PC) == HALTINSTRUCTION) && ((cpu_unit.flags & UNIT_CPU_STOPONHALT) == 0)) {
                    PUSH(PC + 1);
                    PCQ_ENTRY(PC);
                }
                else {
                    PUSH(PC);
                    PCQ_ENTRY(PC - 1);
                }
                PC = timerInterruptHandler & ADDRMASK;
            }

            if (keyboardInterrupt && (IFF_S & 1)) {
                keyboardInterrupt = FALSE;
                specialProcessing = clockFrequency | sim_brk_summ;
                IFF_S = 0; /* disable interrupts */
                CHECK_BREAK_TWO_BYTES_EXTENDED(SP - 2, SP - 1, (keyboardInterrupt = TRUE, IFF_S |= 1));
                if ((GetBYTE(PC) == HALTINSTRUCTION) && ((cpu_unit.flags & UNIT_CPU_STOPONHALT) == 0)) {
                    PUSH(PC + 1);
                    PCQ_ENTRY(PC);
                }
                else {
                    PUSH(PC);
                    PCQ_ENTRY(PC - 1);
                }
                PC = keyboardInterruptHandler & ADDRMASK;
            }

            if (sim_brk_summ) {
                if (sim_brk_test(PC, (2u << SIM_BKPT_V_SPC) | SWMASK('E'))) {           /* breakpoint?              */
                    reason = STOP_IBKPT;                        /* stop simulation  */
                    break;
                }
                if (sim_brk_test(GetBYTE(PC), (1u << SIM_BKPT_V_SPC) | SWMASK('I'))) {  /* instruction breakpoint?  */
                    reason = STOP_INSTR;                                                /* stop simulation          */
                    prepareInstructionMessage(PC, GetBYTE(PC));
                    break;
                }
            }
        }

        PCX = PC;
        sim_interval--;

        /*  make sure that each instructions properly sets sim_brk_pend:
            1) Either directly to FALSE if no memory access takes place or
            2) through a call to a Check... routine
        */
        switch(RAM_PP(PC)) {

            case 0x00:      /* NOP */
                tStates += 4;   /* NOP 4 */
                sim_brk_pend[0] = FALSE;
                break;

            case 0x01:      /* LD BC,nnnn */
                tStates += 10; /* LXI B,nnnn 10 */
                sim_brk_pend[0] = FALSE;
                BC = GET_WORD(PC);
                PC += 2;
                break;

            case 0x02:      /* LD (BC),A */
                tStates += 7; /* STAX B 7 */
                CHECK_BREAK_BYTE(BC)
                PutBYTE(BC, HIGH_REGISTER(AF));
                break;

            case 0x03:      /* INC BC */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 6); /* INX B 5 */
                sim_brk_pend[0] = FALSE;
                ++BC;
                break;

            case 0x04:      /* INC B */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* INR B 5 */
                sim_brk_pend[0] = FALSE;
                BC += 0x100;
                temp = HIGH_REGISTER(BC);
                AF = (AF & ~0xfe) | incTable[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
                break;

            case 0x05:      /* DEC B */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* DCR B 5 */
                sim_brk_pend[0] = FALSE;
                BC -= 0x100;
                temp = HIGH_REGISTER(BC);
                AF = (AF & ~0xfe) | decTable[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
                break;

            case 0x06:      /* LD B,nn */
                tStates += 7; /* MVI B,nn 7 */
                sim_brk_pend[0] = FALSE;
                SET_HIGH_REGISTER(BC, RAM_PP(PC));
                break;

            case 0x07:      /* RLCA */
                tStates += 4; /* RLC 4 */
                sim_brk_pend[0] = FALSE;
                AF = ((AF >> 7) & 0x0128) | ((AF << 1) & ~0x1ff) |
                    (AF & 0xc4) | ((AF >> 15) & 1);
                break;

            case 0x08:      /* EX AF,AF' */
                tStates += 4; /* NOP 4 */
                sim_brk_pend[0] = FALSE;
                CHECK_CPU_8080;
                temp = AF;
                AF = AF1_S;
                AF1_S = temp;
                break;

            case 0x09:      /* ADD HL,BC */
                tStates += (chiptype == CHIP_TYPE_8080 ? 10 : 11); /* DAD B 10 */
                sim_brk_pend[0] = FALSE;
                HL &= ADDRMASK;
                BC &= ADDRMASK;
                sum = HL + BC;
                AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(HL ^ BC ^ sum) >> 8];
                HL = sum;
                break;

            case 0x0a:      /* LD A,(BC) */
                tStates += 7; /* LDAX B 7 */
                CHECK_BREAK_BYTE(BC)
                SET_HIGH_REGISTER(AF, GetBYTE(BC));
                break;

            case 0x0b:      /* DEC BC */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 6); /* DCX B 5 */
                sim_brk_pend[0] = FALSE;
                --BC;
                break;

            case 0x0c:      /* INC C */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* INR C 5 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(BC) + 1;
                SET_LOW_REGISTER(BC, temp);
                AF = (AF & ~0xfe) | incTable[temp] | SET_PV2(0x80);
                break;

            case 0x0d:      /* DEC C */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* DCR C 5 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(BC) - 1;
                SET_LOW_REGISTER(BC, temp);
                AF = (AF & ~0xfe) | decTable[temp & 0xff] | SET_PV2(0x7f);
                break;

            case 0x0e:      /* LD C,nn */
                tStates += 7; /* MVI C,nn 7 */
                sim_brk_pend[0] = FALSE;
                SET_LOW_REGISTER(BC, RAM_PP(PC));
                break;

            case 0x0f:      /* RRCA */
                tStates += 4; /* RRC 4 */
                sim_brk_pend[0] = FALSE;
                AF = (AF & 0xc4) | rrcaTable[HIGH_REGISTER(AF)];
                break;

            case 0x10:      /* DJNZ dd */
                sim_brk_pend[0] = FALSE;
                if (chiptype == CHIP_TYPE_8080)
                    tStates += 4; /* NOP 4 */
                CHECK_CPU_8080;
                if ((BC -= 0x100) & 0xff00) {
                    PCQ_ENTRY(PCX);
                    PC += (int8) GetBYTE(PC) + 1;
                    tStates += 13;
                }
                else {
                    PC++;
                    tStates += 8;
                }
                break;

            case 0x11:      /* LD DE,nnnn */
                tStates += 10; /* LXI D,nnnn 10 */
                sim_brk_pend[0] = FALSE;
                DE = GET_WORD(PC);
                PC += 2;
                break;

            case 0x12:      /* LD (DE),A */
                tStates += 7; /* STAX D 7 */
                CHECK_BREAK_BYTE(DE)
                PutBYTE(DE, HIGH_REGISTER(AF));
                break;

            case 0x13:      /* INC DE */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 6); /* INX D 5 */
                sim_brk_pend[0] = FALSE;
                ++DE;
                break;

            case 0x14:      /* INC D */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* INR D 5 */
                sim_brk_pend[0] = FALSE;
                DE += 0x100;
                temp = HIGH_REGISTER(DE);
                AF = (AF & ~0xfe) | incTable[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
                break;

            case 0x15:      /* DEC D */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* DCR D 5 */
                sim_brk_pend[0] = FALSE;
                DE -= 0x100;
                temp = HIGH_REGISTER(DE);
                AF = (AF & ~0xfe) | decTable[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
                break;

            case 0x16:      /* LD D,nn */
                tStates += 7; /* MVI D,nn 7 */
                sim_brk_pend[0] = FALSE;
                SET_HIGH_REGISTER(DE, RAM_PP(PC));
                break;

            case 0x17:      /* RLA */
                tStates += 4; /* RAL 4 */
                sim_brk_pend[0] = FALSE;
                AF = ((AF << 8) & 0x0100) | ((AF >> 7) & 0x28) | ((AF << 1) & ~0x01ff) |
                    (AF & 0xc4) | ((AF >> 15) & 1);
                break;

            case 0x18:      /* JR dd */
                tStates += (chiptype == CHIP_TYPE_8080 ? 4 : 12); /* NOP 4 */
                sim_brk_pend[0] = FALSE;
                CHECK_CPU_8080;
                PCQ_ENTRY(PCX);
                PC += (int8) GetBYTE(PC) + 1;
                break;

            case 0x19:      /* ADD HL,DE */
                tStates += (chiptype == CHIP_TYPE_8080 ? 10 : 11); /* DAD D 10 */
                sim_brk_pend[0] = FALSE;
                HL &= ADDRMASK;
                DE &= ADDRMASK;
                sum = HL + DE;
                AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(HL ^ DE ^ sum) >> 8];
                HL = sum;
                break;

            case 0x1a:      /* LD A,(DE) */
                tStates += 7; /* LDAX D 7 */
                CHECK_BREAK_BYTE(DE)
                SET_HIGH_REGISTER(AF, GetBYTE(DE));
                break;

            case 0x1b:      /* DEC DE */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 6); /* DCX D 5 */
                sim_brk_pend[0] = FALSE;
                --DE;
                break;

            case 0x1c:      /* INC E */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* INR E 5 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(DE) + 1;
                SET_LOW_REGISTER(DE, temp);
                AF = (AF & ~0xfe) | incTable[temp] | SET_PV2(0x80);
                break;

            case 0x1d:      /* DEC E */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* DCR E 5 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(DE) - 1;
                SET_LOW_REGISTER(DE, temp);
                AF = (AF & ~0xfe) | decTable[temp & 0xff] | SET_PV2(0x7f);
                break;

            case 0x1e:      /* LD E,nn */
                tStates += 7; /* MVI E 7 */
                sim_brk_pend[0] = FALSE;
                SET_LOW_REGISTER(DE, RAM_PP(PC));
                break;

            case 0x1f:      /* RRA */
                tStates += 4; /* RAR 4 */
                sim_brk_pend[0] = FALSE;
                AF = ((AF & 1) << 15) | (AF & 0xc4) | rraTable[HIGH_REGISTER(AF)];
                break;

            case 0x20:      /* JR NZ,dd */
                if (chiptype == CHIP_TYPE_8080)
                    tStates += 4; /* NOP 4 */
                sim_brk_pend[0] = FALSE;
                CHECK_CPU_8080;
                if (TSTFLAG(Z)) {
                    PC++;
                    tStates += 7;
                }
                else {
                    PCQ_ENTRY(PCX);
                    PC += (int8) GetBYTE(PC) + 1;
                    tStates += 12;
                }
                break;

            case 0x21:      /* LD HL,nnnn */
                tStates += 10; /* LXI H,nnnn 10 */
                sim_brk_pend[0] = FALSE;
                HL = GET_WORD(PC);
                PC += 2;
                break;

            case 0x22:      /* LD (nnnn),HL */
                tStates += 16; /* SHLD 16 */
                temp = GET_WORD(PC);
                CHECK_BREAK_WORD(temp);
                PutWORD(temp, HL);
                PC += 2;
                break;

            case 0x23:      /* INC HL */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 6); /* INX H 5 */
                sim_brk_pend[0] = FALSE;
                ++HL;
                break;

            case 0x24:      /* INC H */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* INR H 5 */
                sim_brk_pend[0] = FALSE;
                HL += 0x100;
                temp = HIGH_REGISTER(HL);
                AF = (AF & ~0xfe) | incTable[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
                break;

            case 0x25:      /* DEC H */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* DCR H 5 */
                sim_brk_pend[0] = FALSE;
                HL -= 0x100;
                temp = HIGH_REGISTER(HL);
                AF = (AF & ~0xfe) | decTable[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
                break;

            case 0x26:      /* LD H,nn */
                tStates += 7; /* MVI H,nn 7 */
                sim_brk_pend[0] = FALSE;
                SET_HIGH_REGISTER(HL, RAM_PP(PC));
                break;

            case 0x27:      /* DAA */
                tStates += 4; /* DAA 4 */
                sim_brk_pend[0] = FALSE;
                acu = HIGH_REGISTER(AF);
                temp = LOW_DIGIT(acu);
                cbits = TSTFLAG(C);
                if (TSTFLAG(N)) {       /* last operation was a subtract */
                    int hd = cbits || acu > 0x99;
                    if (TSTFLAG(H) || (temp > 9)) { /* adjust low digit */
                        if (temp > 5) {
                            SETFLAG(H, 0);
                        }
                        acu -= 6;
                        acu &= 0xff;
                    }
                    if (hd)
                        acu -= 0x160;   /* adjust high digit */
                }
                else {          /* last operation was an add */
                    if (TSTFLAG(H) || (temp > 9)) { /* adjust low digit */
                        SETFLAG(H, (temp > 9));
                        acu += 6;
                    }
                    if (cbits || ((acu & 0x1f0) > 0x90))
                        acu += 0x60;   /* adjust high digit */
                }
                AF = (AF & 0x12) | rrdrldTable[acu & 0xff] | ((acu >> 8) & 1) | cbits;
                break;

            case 0x28:      /* JR Z,dd */
                if (chiptype == CHIP_TYPE_8080)
                    tStates += 4; /* NOP 4 */
                sim_brk_pend[0] = FALSE;
                CHECK_CPU_8080;
                if (TSTFLAG(Z)) {
                    PCQ_ENTRY(PCX);
                    PC += (int8) GetBYTE(PC) + 1;
                    tStates += 12;
                }
                else {
                    PC++;
                    tStates += 7;
                }
                break;

            case 0x29:      /* ADD HL,HL */
                tStates += (chiptype == CHIP_TYPE_8080 ? 10 : 11); /* DAD H 10 */
                sim_brk_pend[0] = FALSE;
                HL &= ADDRMASK;
                sum = HL + HL;
                AF = (AF & ~0x3b) | cbitsDup16Table[sum >> 8];
                HL = sum;
                break;

            case 0x2a:      /* LD HL,(nnnn) */
                tStates += 16; /* LHLD nnnn 16 */
                temp = GET_WORD(PC);
                CHECK_BREAK_WORD(temp);
                HL = GET_WORD(temp);
                PC += 2;
                break;

            case 0x2b:      /* DEC HL */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 6); /* DCX H 5 */
                sim_brk_pend[0] = FALSE;
                --HL;
                break;

            case 0x2c:      /* INC L */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* INR L 5 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(HL) + 1;
                SET_LOW_REGISTER(HL, temp);
                AF = (AF & ~0xfe) | incTable[temp] | SET_PV2(0x80);
                break;

            case 0x2d:      /* DEC L */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* DCR L 5 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(HL) - 1;
                SET_LOW_REGISTER(HL, temp);
                AF = (AF & ~0xfe) | decTable[temp & 0xff] | SET_PV2(0x7f);
                break;

            case 0x2e:      /* LD L,nn */
                tStates += 7; /* MVI L,nn 7 */
                sim_brk_pend[0] = FALSE;
                SET_LOW_REGISTER(HL, RAM_PP(PC));
                break;

            case 0x2f:      /* CPL */
                tStates += 4; /* CMA 4 */
                sim_brk_pend[0] = FALSE;
                AF = (~AF & ~0xff) | (AF & 0xc5) | ((~AF >> 8) & 0x28) | 0x12;
                break;

            case 0x30:      /* JR NC,dd */
                if (chiptype == CHIP_TYPE_8080)
                    tStates += 4; /* NOP 4 */
                sim_brk_pend[0] = FALSE;
                CHECK_CPU_8080;
                if (TSTFLAG(C)) {
                    PC++;
                    tStates += 7;
                }
                else {
                    PCQ_ENTRY(PCX);
                    PC += (int8) GetBYTE(PC) + 1;
                    tStates += 12;
                }
                break;

            case 0x31:      /* LD SP,nnnn */
                tStates += 10; /* LXI SP,nnnn 10 */
                sim_brk_pend[0] = FALSE;
                SP = GET_WORD(PC);
                PC += 2;
                break;

            case 0x32:      /* LD (nnnn),A */
                tStates += 13; /* STA nnnn 13 */
                temp = GET_WORD(PC);
                CHECK_BREAK_BYTE(temp);
                PutBYTE(temp, HIGH_REGISTER(AF));
                PC += 2;
                break;

            case 0x33:      /* INC SP */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 6); /* INX SP 5 */
                sim_brk_pend[0] = FALSE;
                ++SP;
                break;

            case 0x34:      /* INC (HL) */
                tStates += (chiptype == CHIP_TYPE_8080 ? 10 : 11); /* INR M 10 */
                CHECK_BREAK_BYTE(HL);
                temp = GetBYTE(HL) + 1;
                PutBYTE(HL, temp);
                AF = (AF & ~0xfe) | incTable[temp] | SET_PV2(0x80);
                break;

            case 0x35:      /* DEC (HL) */
                tStates += (chiptype == CHIP_TYPE_8080 ? 10 : 11); /* DCR M 10 */
                CHECK_BREAK_BYTE(HL);
                temp = GetBYTE(HL) - 1;
                PutBYTE(HL, temp);
                AF = (AF & ~0xfe) | decTable[temp & 0xff] | SET_PV2(0x7f);
                break;

            case 0x36:      /* LD (HL),nn */
                tStates += 10; /* MVI M 10 */
                CHECK_BREAK_BYTE(HL);
                PutBYTE(HL, RAM_PP(PC));
                break;

            case 0x37:      /* SCF */
                tStates += 4; /* STC 4 */
                sim_brk_pend[0] = FALSE;
                AF = (AF & ~0x3b) | ((AF >> 8) & 0x28) | 1;
                break;

            case 0x38:      /* JR C,dd */
                if (chiptype == CHIP_TYPE_8080)
                    tStates += 4; /* NOP 4 */
                sim_brk_pend[0] = FALSE;
                CHECK_CPU_8080;
                if (TSTFLAG(C)) {
                    PCQ_ENTRY(PCX);
                    PC += (int8) GetBYTE(PC) + 1;
                    tStates += 12;
                }
                else {
                    PC++;
                    tStates += 7;
                }
                break;

            case 0x39:      /* ADD HL,SP */
                tStates += (chiptype == CHIP_TYPE_8080 ? 10 : 11); /* DAD SP 10 */
                sim_brk_pend[0] = FALSE;
                HL &= ADDRMASK;
                SP &= ADDRMASK;
                sum = HL + SP;
                AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(HL ^ SP ^ sum) >> 8];
                HL = sum;
                break;

            case 0x3a:      /* LD A,(nnnn) */
                tStates += 13; /* LDA nnnn 13 */
                temp = GET_WORD(PC);
                CHECK_BREAK_BYTE(temp);
                SET_HIGH_REGISTER(AF, GetBYTE(temp));
                PC += 2;
                break;

            case 0x3b:      /* DEC SP */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 6); /* DCX SP 5 */
                sim_brk_pend[0] = FALSE;
                --SP;
                break;

            case 0x3c:      /* INC A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* INR A 5 */
                sim_brk_pend[0] = FALSE;
                AF += 0x100;
                temp = HIGH_REGISTER(AF);
                AF = (AF & ~0xfe) | incTable[temp] | SET_PV2(0x80); /* SET_PV2 uses temp */
                break;

            case 0x3d:      /* DEC A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* DCR A 5 */
                sim_brk_pend[0] = FALSE;
                AF -= 0x100;
                temp = HIGH_REGISTER(AF);
                AF = (AF & ~0xfe) | decTable[temp] | SET_PV2(0x7f); /* SET_PV2 uses temp */
                break;

            case 0x3e:      /* LD A,nn */
                tStates += 7; /* MVI A,nn 7 */
                sim_brk_pend[0] = FALSE;
                SET_HIGH_REGISTER(AF, RAM_PP(PC));
                break;

            case 0x3f:      /* CCF */
                tStates += 4; /* CMC 4 */
                sim_brk_pend[0] = FALSE;
                AF = (AF & ~0x3b) | ((AF >> 8) & 0x28) | ((AF & 1) << 4) | (~AF & 1);
                break;

            case 0x40:      /* LD B,B */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV B,B 5 */
                sim_brk_pend[0] = FALSE; /* nop */
                break;

            case 0x41:      /* LD B,C */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV B,C 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & 0xff) | ((BC & 0xff) << 8);
                break;

            case 0x42:      /* LD B,D */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV B,D 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & 0xff) | (DE & ~0xff);
                break;

            case 0x43:      /* LD B,E */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV B,E 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & 0xff) | ((DE & 0xff) << 8);
                break;

            case 0x44:      /* LD B,H */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV B,H 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & 0xff) | (HL & ~0xff);
                break;

            case 0x45:      /* LD B,L */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV B,L 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & 0xff) | ((HL & 0xff) << 8);
                break;

            case 0x46:      /* LD B,(HL) */
                tStates += 7; /* MOV B,M 7 */
                CHECK_BREAK_BYTE(HL);
                SET_HIGH_REGISTER(BC, GetBYTE(HL));
                break;

            case 0x47:      /* LD B,A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV B,A 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & 0xff) | (AF & ~0xff);
                break;

            case 0x48:      /* LD C,B */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV C,B 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & ~0xff) | ((BC >> 8) & 0xff);
                break;

            case 0x49:      /* LD C,C */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV C,C 5 */
                sim_brk_pend[0] = FALSE; /* nop */
                break;

            case 0x4a:      /* LD C,D */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV C,D 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & ~0xff) | ((DE >> 8) & 0xff);
                break;

            case 0x4b:      /* LD C,E */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV C,E 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & ~0xff) | (DE & 0xff);
                break;

            case 0x4c:      /* LD C,H */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV C,H 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & ~0xff) | ((HL >> 8) & 0xff);
                break;

            case 0x4d:      /* LD C,L */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV C,L 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & ~0xff) | (HL & 0xff);
                break;

            case 0x4e:      /* LD C,(HL) */
                tStates += 7; /* MOV C,M 7 */
                CHECK_BREAK_BYTE(HL);
                SET_LOW_REGISTER(BC, GetBYTE(HL));
                break;

            case 0x4f:      /* LD C,A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV C,A 5 */
                sim_brk_pend[0] = FALSE;
                BC = (BC & ~0xff) | ((AF >> 8) & 0xff);
                break;

            case 0x50:      /* LD D,B */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV D,B 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & 0xff) | (BC & ~0xff);
                break;

            case 0x51:      /* LD D,C */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV D,C 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & 0xff) | ((BC & 0xff) << 8);
                break;

            case 0x52:      /* LD D,D */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV D,D 5 */
                sim_brk_pend[0] = FALSE; /* nop */
                break;

            case 0x53:      /* LD D,E */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV D,E 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & 0xff) | ((DE & 0xff) << 8);
                break;

            case 0x54:      /* LD D,H */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV D,H 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & 0xff) | (HL & ~0xff);
                break;

            case 0x55:      /* LD D,L */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV D,L 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & 0xff) | ((HL & 0xff) << 8);
                break;

            case 0x56:      /* LD D,(HL) */
                tStates += 7; /* MOV D,M 7 */
                CHECK_BREAK_BYTE(HL);
                SET_HIGH_REGISTER(DE, GetBYTE(HL));
                break;

            case 0x57:      /* LD D,A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV D,A 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & 0xff) | (AF & ~0xff);
                break;

            case 0x58:      /* LD E,B */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV E,B 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & ~0xff) | ((BC >> 8) & 0xff);
                break;

            case 0x59:      /* LD E,C */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV E,C 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & ~0xff) | (BC & 0xff);
                break;

            case 0x5a:      /* LD E,D */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV E,D 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & ~0xff) | ((DE >> 8) & 0xff);
                break;

            case 0x5b:      /* LD E,E */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV E,E 5 */
                sim_brk_pend[0] = FALSE; /* nop */
                break;

            case 0x5c:      /* LD E,H */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV E,H 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & ~0xff) | ((HL >> 8) & 0xff);
                break;

            case 0x5d:      /* LD E,L */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV E,L 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & ~0xff) | (HL & 0xff);
                break;

            case 0x5e:      /* LD E,(HL) */
                tStates += 7; /* MOV E,M 7 */
                CHECK_BREAK_BYTE(HL);
                SET_LOW_REGISTER(DE, GetBYTE(HL));
                break;

            case 0x5f:      /* LD E,A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV E,A 5 */
                sim_brk_pend[0] = FALSE;
                DE = (DE & ~0xff) | ((AF >> 8) & 0xff);
                break;

            case 0x60:      /* LD H,B */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV H,B 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & 0xff) | (BC & ~0xff);
                break;

            case 0x61:      /* LD H,C */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV H,C 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & 0xff) | ((BC & 0xff) << 8);
                break;

            case 0x62:      /* LD H,D */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV H,D 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & 0xff) | (DE & ~0xff);
                break;

            case 0x63:      /* LD H,E */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV H,E 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & 0xff) | ((DE & 0xff) << 8);
                break;

            case 0x64:      /* LD H,H */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV H,H 5 */
                sim_brk_pend[0] = FALSE; /* nop */
                break;

            case 0x65:      /* LD H,L */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV H,L 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & 0xff) | ((HL & 0xff) << 8);
                break;

            case 0x66:      /* LD H,(HL) */
                tStates += 7; /* MOV H,M 7 */
                CHECK_BREAK_BYTE(HL);
                SET_HIGH_REGISTER(HL, GetBYTE(HL));
                break;

            case 0x67:      /* LD H,A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV H,A 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & 0xff) | (AF & ~0xff);
                break;

            case 0x68:      /* LD L,B */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV L,B 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & ~0xff) | ((BC >> 8) & 0xff);
                break;

            case 0x69:      /* LD L,C */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV L,C 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & ~0xff) | (BC & 0xff);
                break;

            case 0x6a:      /* LD L,D */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV L,D 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & ~0xff) | ((DE >> 8) & 0xff);
                break;

            case 0x6b:      /* LD L,E */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV L,E 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & ~0xff) | (DE & 0xff);
                break;

            case 0x6c:      /* LD L,H */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV L,H 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & ~0xff) | ((HL >> 8) & 0xff);
                break;

            case 0x6d:      /* LD L,L */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV L,L 5 */
                sim_brk_pend[0] = FALSE; /* nop */
                break;

            case 0x6e:      /* LD L,(HL) */
                tStates += 7; /* MOV L,M 7 */
                CHECK_BREAK_BYTE(HL);
                SET_LOW_REGISTER(HL, GetBYTE(HL));
                break;

            case 0x6f:      /* LD L,A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV L,A 5 */
                sim_brk_pend[0] = FALSE;
                HL = (HL & ~0xff) | ((AF >> 8) & 0xff);
                break;

            case 0x70:      /* LD (HL),B */
                tStates += 7; /* MOV M,B 7 */
                CHECK_BREAK_BYTE(HL);
                PutBYTE(HL, HIGH_REGISTER(BC));
                break;

            case 0x71:      /* LD (HL),C */
                tStates += 7; /* MOV M,C 7 */
                CHECK_BREAK_BYTE(HL);
                PutBYTE(HL, LOW_REGISTER(BC));
                break;

            case 0x72:      /* LD (HL),D */
                tStates += 7; /* MOV M,D 7 */
                CHECK_BREAK_BYTE(HL);
                PutBYTE(HL, HIGH_REGISTER(DE));
                break;

            case 0x73:      /* LD (HL),E */
                tStates += 7; /* MOV M,E 7 */
                CHECK_BREAK_BYTE(HL);
                PutBYTE(HL, LOW_REGISTER(DE));
                break;

            case 0x74:      /* LD (HL),H */
                tStates += 7; /* MOV M,H 7 */
                CHECK_BREAK_BYTE(HL);
                PutBYTE(HL, HIGH_REGISTER(HL));
                break;

            case 0x75:      /* LD (HL),L */
                tStates += 7; /* MOV M,L 7 */
                CHECK_BREAK_BYTE(HL);
                PutBYTE(HL, LOW_REGISTER(HL));
                break;

            case HALTINSTRUCTION:   /* HALT */
                tStates += (chiptype == CHIP_TYPE_8080 ? 7 : 4); /* HLT 7 */
                sim_brk_pend[0] = FALSE;
                PC--;
                if (cpu_unit.flags & UNIT_CPU_STOPONHALT) {
                    reason = STOP_HALT;
                    goto end_decode;
                }
                sim_interval = 0;
                do_SIMH_sleep();    /* reduce CPU load in busy wait */
                break;

            case 0x77:      /* LD (HL),A */
                tStates += 7; /* MOV M,A 7 */
                CHECK_BREAK_BYTE(HL);
                PutBYTE(HL, HIGH_REGISTER(AF));
                break;

            case 0x78:      /* LD A,B */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV A,B 5 */
                sim_brk_pend[0] = FALSE;
                AF = (AF & 0xff) | (BC & ~0xff);
                break;

            case 0x79:      /* LD A,C */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV A,C 5 */
                sim_brk_pend[0] = FALSE;
                AF = (AF & 0xff) | ((BC & 0xff) << 8);
                break;

            case 0x7a:      /* LD A,D */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV A,D 5 */
                sim_brk_pend[0] = FALSE;
                AF = (AF & 0xff) | (DE & ~0xff);
                break;

            case 0x7b:      /* LD A,E */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV A,E 5 */
                sim_brk_pend[0] = FALSE;
                AF = (AF & 0xff) | ((DE & 0xff) << 8);
                break;

            case 0x7c:      /* LD A,H */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV A,H 5 */
                sim_brk_pend[0] = FALSE;
                AF = (AF & 0xff) | (HL & ~0xff);
                break;

            case 0x7d:      /* LD A,L */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV A,L 5 */
                sim_brk_pend[0] = FALSE;
                AF = (AF & 0xff) | ((HL & 0xff) << 8);
                break;

            case 0x7e:      /* LD A,(HL) */
                tStates += 7; /* MOV A,M 7 */
                CHECK_BREAK_BYTE(HL);
                SET_HIGH_REGISTER(AF, GetBYTE(HL));
                break;

            case 0x7f:      /* LD A,A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* MOV A,A 5 */
                sim_brk_pend[0] = FALSE; /* nop */
                break;

            case 0x80:      /* ADD A,B */
                tStates += 4; /* ADD B 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(BC);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp;
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x81:      /* ADD A,C */
                tStates += 4; /* ADD C 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(BC);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp;
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x82:      /* ADD A,D */
                tStates += 4; /* ADD D 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(DE);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp;
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x83:      /* ADD A,E */
                tStates += 4; /* ADD E 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(DE);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp;
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x84:      /* ADD A,H */
                tStates += 4; /* ADD H 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp;
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x85:      /* ADD A,L */
                tStates += 4; /* ADD L 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp;
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x86:      /* ADD A,(HL) */
                tStates += 7; /* ADD M 7 */
                CHECK_BREAK_BYTE(HL);
                temp = GetBYTE(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp;
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x87:      /* ADD A,A */
                tStates += 4; /* ADD A 4 */
                sim_brk_pend[0] = FALSE;
                cbits = 2 * HIGH_REGISTER(AF);
                AF = cbitsDup8Table[cbits] | (SET_PVS(cbits));
                break;

            case 0x88:      /* ADC A,B */
                tStates += 4; /* ADC B 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(BC);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp + TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x89:      /* ADC A,C */
                tStates += 4; /* ADC C 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(BC);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp + TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x8a:      /* ADC A,D */
                tStates += 4; /* ADC D 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(DE);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp + TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x8b:      /* ADC A,E */
                tStates += 4; /* ADC E 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(DE);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp + TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x8c:      /* ADC A,H */
                tStates += 4; /* ADC H 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp + TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x8d:      /* ADC A,L */
                tStates += 4; /* ADC L 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp + TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x8e:      /* ADC A,(HL) */
                tStates += 7; /* ADC M 7 */
                CHECK_BREAK_BYTE(HL);
                temp = GetBYTE(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp + TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0x8f:      /* ADC A,A */
                tStates += 4; /* ADC A 4 */
                sim_brk_pend[0] = FALSE;
                cbits = 2 * HIGH_REGISTER(AF) + TSTFLAG(C);
                AF = cbitsDup8Table[cbits] | (SET_PVS(cbits));
                break;

            case 0x90:      /* SUB B */
                tStates += 4; /* SUB B 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(BC);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x91:      /* SUB C */
                tStates += 4; /* SUB C 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(BC);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x92:      /* SUB D */
                tStates += 4; /* SUB D 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(DE);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x93:      /* SUB E */
                tStates += 4; /* SUB E 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(DE);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x94:      /* SUB H */
                tStates += 4; /* SUB H 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x95:      /* SUB L */
                tStates += 4; /* SUB L 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x96:      /* SUB (HL) */
                tStates += 7; /* SUB M 7 */
                CHECK_BREAK_BYTE(HL);
                temp = GetBYTE(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x97:      /* SUB A */
                tStates += 4; /* SUB A 4 */
                sim_brk_pend[0] = FALSE;
                AF = (chiptype == CHIP_TYPE_Z80) ? 0x42 : 0x46;
                break;

            case 0x98:      /* SBC A,B */
                tStates += 4; /* SBB B 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(BC);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp - TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x99:      /* SBC A,C */
                tStates += 4; /* SBB C 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(BC);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp - TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x9a:      /* SBC A,D */
                tStates += 4; /* SBB D 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(DE);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp - TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x9b:      /* SBC A,E */
                tStates += 4; /* SBB E 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(DE);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp - TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x9c:      /* SBC A,H */
                tStates += 4; /* SBB H 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp - TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x9d:      /* SBC A,L */
                tStates += 4; /* SBB L 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp - TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x9e:      /* SBC A,(HL) */
                tStates += 7; /* SBB M 7 */
                CHECK_BREAK_BYTE(HL);
                temp = GetBYTE(HL);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp - TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0x9f:      /* SBC A,A */
                tStates += 4; /* SBB A 4 */
                sim_brk_pend[0] = FALSE;
                cbits = -TSTFLAG(C);
                AF = subTable[cbits & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PVS(cbits));
                break;

            case 0xa0:      /* AND B */
                tStates += 4; /* ANA B 4 */
                sim_brk_pend[0] = FALSE;
                AF = andTable[((AF & BC) >> 8) & 0xff];
                break;

            case 0xa1:      /* AND C */
                tStates += 4; /* ANA C 4 */
                sim_brk_pend[0] = FALSE;
                AF = andTable[((AF >> 8) & BC) & 0xff];
                break;

            case 0xa2:      /* AND D */
                tStates += 4; /* ANA D 4 */
                sim_brk_pend[0] = FALSE;
                AF = andTable[((AF & DE) >> 8) & 0xff];
                break;

            case 0xa3:      /* AND E */
                tStates += 4; /* ANA E 4 */
                sim_brk_pend[0] = FALSE;
                AF = andTable[((AF >> 8) & DE) & 0xff];
                break;

            case 0xa4:      /* AND H */
                tStates += 4; /* ANA H 4 */
                sim_brk_pend[0] = FALSE;
                AF = andTable[((AF & HL) >> 8) & 0xff];
                break;

            case 0xa5:      /* AND L */
                tStates += 4; /* ANA L 4 */
                sim_brk_pend[0] = FALSE;
                AF = andTable[((AF >> 8) & HL) & 0xff];
                break;

            case 0xa6:      /* AND (HL) */
                tStates += 7; /* ANA M 7 */
                CHECK_BREAK_BYTE(HL);
                AF = andTable[((AF >> 8) & GetBYTE(HL)) & 0xff];
                break;

            case 0xa7:      /* AND A */
                tStates += 4; /* ANA A 4 */
                sim_brk_pend[0] = FALSE;
                AF = andTable[(AF >> 8) & 0xff];
                break;

            case 0xa8:      /* XOR B */
                tStates += 4; /* XRA B 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF ^ BC) >> 8) & 0xff];
                break;

            case 0xa9:      /* XOR C */
                tStates += 4; /* XRA C 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF >> 8) ^ BC) & 0xff];
                break;

            case 0xaa:      /* XOR D */
                tStates += 4; /* XRA D 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF ^ DE) >> 8) & 0xff];
                break;

            case 0xab:      /* XOR E */
                tStates += 4; /* XRA E 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF >> 8) ^ DE) & 0xff];
                break;

            case 0xac:      /* XOR H */
                tStates += 4; /* XRA H 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF ^ HL) >> 8) & 0xff];
                break;

            case 0xad:      /* XOR L */
                tStates += 4; /* XRA L 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF >> 8) ^ HL) & 0xff];
                break;

            case 0xae:      /* XOR (HL) */
                tStates += 7; /* XRA M 7 */
                CHECK_BREAK_BYTE(HL);
                AF = xororTable[((AF >> 8) ^ GetBYTE(HL)) & 0xff];
                break;

            case 0xaf:      /* XOR A */
                tStates += 4; /* XRA A 4 */
                sim_brk_pend[0] = FALSE;
                AF = 0x44;
                break;

            case 0xb0:      /* OR B */
                tStates += 4; /* ORA B 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF | BC) >> 8) & 0xff];
                break;

            case 0xb1:      /* OR C */
                tStates += 4; /* ORA C 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF >> 8) | BC) & 0xff];
                break;

            case 0xb2:      /* OR D */
                tStates += 4; /* ORA D 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF | DE) >> 8) & 0xff];
                break;

            case 0xb3:      /* OR E */
                tStates += 4; /* ORA E 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF >> 8) | DE) & 0xff];
                break;

            case 0xb4:      /* OR H */
                tStates += 4; /* ORA H 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF | HL) >> 8) & 0xff];
                break;

            case 0xb5:      /* OR L */
                tStates += 4; /* ORA L 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF >> 8) | HL) & 0xff];
                break;

            case 0xb6:      /* OR (HL) */
                tStates += 7; /* ORA M 7 */
                CHECK_BREAK_BYTE(HL);
                AF = xororTable[((AF >> 8) | GetBYTE(HL)) & 0xff];
                break;

            case 0xb7:      /* OR A */
                tStates += 4; /* ORA A 4 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[(AF >> 8) & 0xff];
                break;

            case 0xb8:      /* CP B */
                tStates += 4; /* CMP B 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(BC);
                AF = (AF & ~0x28) | (temp & 0x28);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                    (SET_PV) | cbits2Table[cbits & 0x1ff];
                break;

            case 0xb9:      /* CP C */
                tStates += 4; /* CMP C 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(BC);
                AF = (AF & ~0x28) | (temp & 0x28);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                    (SET_PV) | cbits2Table[cbits & 0x1ff];
                break;

            case 0xba:      /* CP D */
                tStates += 4; /* CMP D 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(DE);
                AF = (AF & ~0x28) | (temp & 0x28);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                    (SET_PV) | cbits2Table[cbits & 0x1ff];
                break;

            case 0xbb:      /* CP E */
                tStates += 4; /* CMP E 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(DE);
                AF = (AF & ~0x28) | (temp & 0x28);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                    (SET_PV) | cbits2Table[cbits & 0x1ff];
                break;

            case 0xbc:      /* CP H */
                tStates += 4; /* CMP H 4 */
                sim_brk_pend[0] = FALSE;
                temp = HIGH_REGISTER(HL);
                AF = (AF & ~0x28) | (temp & 0x28);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                    (SET_PV) | cbits2Table[cbits & 0x1ff];
                break;

            case 0xbd:      /* CP L */
                tStates += 4; /* CMP L 4 */
                sim_brk_pend[0] = FALSE;
                temp = LOW_REGISTER(HL);
                AF = (AF & ~0x28) | (temp & 0x28);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                    (SET_PV) | cbits2Table[cbits & 0x1ff];
                break;

            case 0xbe:      /* CP (HL) */
                tStates += 7; /* CMP M 7 */
                CHECK_BREAK_BYTE(HL);
                temp = GetBYTE(HL);
                AF = (AF & ~0x28) | (temp & 0x28);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                    (SET_PV) | cbits2Table[cbits & 0x1ff];
                break;

            case 0xbf:      /* CP A */
                tStates += 4; /* CMP A 4 */
                sim_brk_pend[0] = FALSE;
                SET_LOW_REGISTER(AF, (HIGH_REGISTER(AF) & 0x28) | (chiptype == CHIP_TYPE_Z80 ? 0x42 : 0x46));
                break;

            case 0xc0:      /* RET NZ */
                if (TSTFLAG(Z)) {
                    sim_brk_pend[0] = FALSE;
                    tStates += 5; /* RNZ 5 */
                }
                else {
                    CHECK_BREAK_WORD(SP);
                    PCQ_ENTRY(PCX);
                    POP(PC);
                    tStates += 11; /* RNZ 11 */
                }
                break;

            case 0xc1:      /* POP BC */
                tStates += 10; /* POP B 10 */
                CHECK_BREAK_WORD(SP);
                POP(BC);
                break;

            case 0xc2:      /* JP NZ,nnnn */
                sim_brk_pend[0] = FALSE;
                JPC(!TSTFLAG(Z));       /* also updates tStates, Z80 and 8080 are equal */
                break;

            case 0xc3:      /* JP nnnn */
                sim_brk_pend[0] = FALSE;
                JPC(1);                 /* also updates tStates, Z80 and 8080 are equal */
                break;

            case 0xc4:      /* CALL NZ,nnnn */
                CALLC(!TSTFLAG(Z));     /* also updates tStates */
                break;

            case 0xc5:      /* PUSH BC */
                tStates += 11; /* PUSH B 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(BC);
                break;

            case 0xc6:      /* ADD A,nn */
                tStates += 7; /* ADI nn 7 */
                sim_brk_pend[0] = FALSE;
                temp = RAM_PP(PC);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp;
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0xc7:      /* RST 0 */
                tStates += 11; /* RST 0 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(PC);
                PCQ_ENTRY(PCX);
                PC = 0;
                break;

            case 0xc8:      /* RET Z */
                if (TSTFLAG(Z)) {
                    CHECK_BREAK_WORD(SP);
                    PCQ_ENTRY(PCX);
                    POP(PC);
                    tStates += 11; /* RZ 11 */
                }
                else {
                    sim_brk_pend[0] = FALSE;
                    tStates += 5; /* RZ 5 */
                }
                break;

            case 0xc9:      /* RET */
                tStates += 10; /* RET 10 */
                CHECK_BREAK_WORD(SP);
                PCQ_ENTRY(PCX);
                POP(PC);
                break;

            case 0xca:      /* JP Z,nnnn */
                sim_brk_pend[0] = FALSE;
                JPC(TSTFLAG(Z));        /* also updates tStates */
                break;

            case 0xcb:      /* CB prefix */
                if (chiptype == CHIP_TYPE_8080) {
                    if (cpu_unit.flags & UNIT_CPU_OPSTOP) {
                        reason = STOP_OPCODE;
                        goto end_decode;
                    }
                    else {
                        sim_brk_pend[0] = FALSE;
                        JPC(1);
                        break;
                    }
                }
                adr = HL;
                switch ((op = GetBYTE(PC)) & 7) {

                    case 0:
                        sim_brk_pend[0] = tStateModifier = FALSE;
                        ++PC;
                        acu = HIGH_REGISTER(BC);
                        tStates += 8;
                        break;

                    case 1:
                        sim_brk_pend[0] = tStateModifier = FALSE;
                        ++PC;
                        acu = LOW_REGISTER(BC);
                        tStates += 8;
                        break;

                    case 2:
                        sim_brk_pend[0] = tStateModifier = FALSE;
                        ++PC;
                        acu = HIGH_REGISTER(DE);
                        tStates += 8;
                        break;

                    case 3:
                        sim_brk_pend[0] = tStateModifier = FALSE;
                        ++PC;
                        acu = LOW_REGISTER(DE);
                        tStates += 8;
                        break;

                    case 4:
                        sim_brk_pend[0] = tStateModifier = FALSE;
                        ++PC;
                        acu = HIGH_REGISTER(HL);
                        tStates += 8;
                        break;

                    case 5:
                        sim_brk_pend[0] = tStateModifier = FALSE;
                        ++PC;
                        acu = LOW_REGISTER(HL);
                        tStates += 8;
                        break;

                    case 6:
                        CHECK_BREAK_BYTE(adr);
                        ++PC;
                        acu = GetBYTE(adr);
                        tStateModifier = TRUE;
                        tStates += 15;
                        break;

                    case 7:
                        sim_brk_pend[0] = tStateModifier = FALSE;
                        ++PC;
                        acu = HIGH_REGISTER(AF);
                        tStates += 8;
                        break;
                }
                switch (op & 0xc0) {

                    case 0x00:  /* shift/rotate */
                        switch (op & 0x38) {

                            case 0x00:  /* RLC */
                                temp = (acu << 1) | (acu >> 7);
                                cbits = temp & 1;
                                goto cbshflg1;

                            case 0x08:  /* RRC */
                                temp = (acu >> 1) | (acu << 7);
                                cbits = temp & 0x80;
                                goto cbshflg1;

                            case 0x10:  /* RL */
                                temp = (acu << 1) | TSTFLAG(C);
                                cbits = acu & 0x80;
                                goto cbshflg1;

                            case 0x18:  /* RR */
                                temp = (acu >> 1) | (TSTFLAG(C) << 7);
                                cbits = acu & 1;
                                goto cbshflg1;

                            case 0x20:  /* SLA */
                                temp = acu << 1;
                                cbits = acu & 0x80;
                                goto cbshflg1;

                            case 0x28:  /* SRA */
                                temp = (acu >> 1) | (acu & 0x80);
                                cbits = acu & 1;
                                goto cbshflg1;

                            case 0x30:  /* SLIA */
                                temp = (acu << 1) | 1;
                                cbits = acu & 0x80;
                                goto cbshflg1;

                            case 0x38:  /* SRL */
                                temp = acu >> 1;
                                cbits = acu & 1;
                                cbshflg1:
                                AF = (AF & ~0xff) | rotateShiftTable[temp & 0xff] | !!cbits;
                                /* !!cbits == 0 if cbits == 0 !!cbits == 1 if cbits > 0 */
                        }
                        break;

                    case 0x40:  /* BIT */
                        if (tStateModifier)
                            tStates -= 3;
                        if (acu & (1 << ((op >> 3) & 7)))
                            AF = (AF & ~0xfe) | 0x10 | (((op & 0x38) == 0x38) << 7);
                        else
                            AF = (AF & ~0xfe) | 0x54;
                        if ((op & 7) != 6)
                            AF |= (acu & 0x28);
                        temp = acu;
                        break;

                    case 0x80:  /* RES */
                        temp = acu & ~(1 << ((op >> 3) & 7));
                        break;

                    case 0xc0:  /* SET */
                        temp = acu | (1 << ((op >> 3) & 7));
                        break;
                }

                switch (op & 7) {

                    case 0:
                        SET_HIGH_REGISTER(BC, temp);
                        break;

                    case 1:
                        SET_LOW_REGISTER(BC, temp);
                        break;

                    case 2:
                        SET_HIGH_REGISTER(DE, temp);
                        break;

                    case 3:
                        SET_LOW_REGISTER(DE, temp);
                        break;

                    case 4:
                        SET_HIGH_REGISTER(HL, temp);
                        break;

                    case 5:
                        SET_LOW_REGISTER(HL, temp);
                        break;

                    case 6:
                        PutBYTE(adr, temp);
                        break;

                    case 7:
                        SET_HIGH_REGISTER(AF, temp);
                        break;
                }
                break;

            case 0xcc:      /* CALL Z,nnnn */
                CALLC(TSTFLAG(Z));      /* also updates tStates */
                break;

            case 0xcd:      /* CALL nnnn */
                CALLC(1);               /* also updates tStates */
                break;

            case 0xce:      /* ADC A,nn */
                tStates += 7; /* ACI nn 7 */
                sim_brk_pend[0] = FALSE;
                temp = RAM_PP(PC);
                acu = HIGH_REGISTER(AF);
                sum = acu + temp + TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = addTable[sum] | cbitsTable[cbits] | (SET_PV);
                break;

            case 0xcf:      /* RST 8 */
                tStates += 11; /* RST 1 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(PC);
                PCQ_ENTRY(PCX);
                PC = 8;
                break;

            case 0xd0:      /* RET NC */
                if (TSTFLAG(C)) {
                    sim_brk_pend[0] = FALSE;
                    tStates += 5; /* RNC 5 */
                }
                else {
                    CHECK_BREAK_WORD(SP);
                    PCQ_ENTRY(PCX);
                    POP(PC);
                    tStates += 11; /* RNC 11 */
                }
                break;

            case 0xd1:      /* POP DE */
                tStates += 10; /* POP D 10 */
                CHECK_BREAK_WORD(SP);
                POP(DE);
                break;

            case 0xd2:      /* JP NC,nnnn */
                sim_brk_pend[0] = FALSE;
                JPC(!TSTFLAG(C));       /* also updates tStates */
                break;

            case 0xd3:      /* OUT (nn),A */
                tStates += (chiptype == CHIP_TYPE_8080 ? 10 :11); /* OUT nn 10 */
                sim_brk_pend[0] = FALSE;
                out(RAM_PP(PC), HIGH_REGISTER(AF));
                break;

            case 0xd4:      /* CALL NC,nnnn */
                CALLC(!TSTFLAG(C));     /* also updates tStates */
                break;

            case 0xd5:      /* PUSH DE */
                tStates += 11; /* PUSH D 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(DE);
                break;

            case 0xd6:      /* SUB nn */
                tStates += 7; /* SUI nn 7 */
                sim_brk_pend[0] = FALSE;
                temp = RAM_PP(PC);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp;
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0xd7:      /* RST 10H */
                tStates += 11; /* RST 2 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(PC);
                PCQ_ENTRY(PCX);
                PC = 0x10;
                break;

            case 0xd8:      /* RET C */
                if (TSTFLAG(C)) {
                    CHECK_BREAK_WORD(SP);
                    PCQ_ENTRY(PCX);
                    POP(PC);
                    tStates += 11; /* RC 11 */
                }
                else {
                    sim_brk_pend[0] = FALSE;
                    tStates += 5; /* RC 5 */
                }
                break;

            case 0xd9:      /* EXX */
                if (chiptype == CHIP_TYPE_8080) {
                    if (cpu_unit.flags & UNIT_CPU_OPSTOP) {
                        reason = STOP_OPCODE;
                        goto end_decode;
                    }
                    else {
                        tStates += 10; /* RET 10 */
                        CHECK_BREAK_WORD(SP);
                        PCQ_ENTRY(PCX);
                        POP(PC);
                        break;
                    }
                }
                tStates += 4;
                sim_brk_pend[0] = FALSE;
                temp = BC;
                BC = BC1_S;
                BC1_S = temp;
                temp = DE;
                DE = DE1_S;
                DE1_S = temp;
                temp = HL;
                HL = HL1_S;
                HL1_S = temp;
                break;

            case 0xda:      /* JP C,nnnn */
                sim_brk_pend[0] = FALSE;
                JPC(TSTFLAG(C));        /* also updates tStates */
                break;

            case 0xdb:      /* IN A,(nn) */
                tStates += (chiptype == CHIP_TYPE_8080 ? 10 : 11); /* IN nn 10 */
                sim_brk_pend[0] = FALSE;
                SET_HIGH_REGISTER(AF, in(RAM_PP(PC)));
                break;

            case 0xdc:      /* CALL C,nnnn */
                CALLC(TSTFLAG(C));      /* also updates tStates */
                break;

            case 0xdd:      /* DD prefix */
                if (chiptype == CHIP_TYPE_8080) {
                    if (cpu_unit.flags & UNIT_CPU_OPSTOP) {
                        reason = STOP_OPCODE;
                        goto end_decode;
                    }
                    else {
                        CALLC(1);       /* also updates tStates */
                        break;
                    }
                }
                switch (RAM_PP(PC)) {

                    case 0x09:      /* ADD IX,BC */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        IX &= ADDRMASK;
                        BC &= ADDRMASK;
                        sum = IX + BC;
                        AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(IX ^ BC ^ sum) >> 8];
                        IX = sum;
                        break;

                    case 0x19:      /* ADD IX,DE */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        IX &= ADDRMASK;
                        DE &= ADDRMASK;
                        sum = IX + DE;
                        AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(IX ^ DE ^ sum) >> 8];
                        IX = sum;
                        break;

                    case 0x21:      /* LD IX,nnnn */
                        tStates += 14;
                        sim_brk_pend[0] = FALSE;
                        IX = GET_WORD(PC);
                        PC += 2;
                        break;

                    case 0x22:      /* LD (nnnn),IX */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        PutWORD(temp, IX);
                        PC += 2;
                        break;

                    case 0x23:      /* INC IX */
                        tStates += 10;
                        sim_brk_pend[0] = FALSE;
                        ++IX;
                        break;

                    case 0x24:      /* INC IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        IX += 0x100;
                        AF = (AF & ~0xfe) | incZ80Table[HIGH_REGISTER(IX)];
                        break;

                    case 0x25:      /* DEC IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        IX -= 0x100;
                        AF = (AF & ~0xfe) | decZ80Table[HIGH_REGISTER(IX)];
                        break;

                    case 0x26:      /* LD IXH,nn */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IX, RAM_PP(PC));
                        break;

                    case 0x29:      /* ADD IX,IX */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        IX &= ADDRMASK;
                        sum = IX + IX;
                        AF = (AF & ~0x3b) | cbitsDup16Table[sum >> 8];
                        IX = sum;
                        break;

                    case 0x2a:      /* LD IX,(nnnn) */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        IX = GET_WORD(temp);
                        PC += 2;
                        break;

                    case 0x2b:      /* DEC IX */
                        tStates += 10;
                        sim_brk_pend[0] = FALSE;
                        --IX;
                        break;

                    case 0x2c:      /* INC IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IX) + 1;
                        SET_LOW_REGISTER(IX, temp);
                        AF = (AF & ~0xfe) | incZ80Table[temp];
                        break;

                    case 0x2d:      /* DEC IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IX) - 1;
                        SET_LOW_REGISTER(IX, temp);
                        AF = (AF & ~0xfe) | decZ80Table[temp & 0xff];
                        break;

                    case 0x2e:      /* LD IXL,nn */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IX, RAM_PP(PC));
                        break;

                    case 0x34:      /* INC (IX+dd) */
                        tStates += 23;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr) + 1;
                        PutBYTE(adr, temp);
                        AF = (AF & ~0xfe) | incZ80Table[temp];
                        break;

                    case 0x35:      /* DEC (IX+dd) */
                        tStates += 23;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr) - 1;
                        PutBYTE(adr, temp);
                        AF = (AF & ~0xfe) | decZ80Table[temp & 0xff];
                        break;

                    case 0x36:      /* LD (IX+dd),nn */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, RAM_PP(PC));
                        break;

                    case 0x39:      /* ADD IX,SP */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        IX &= ADDRMASK;
                        SP &= ADDRMASK;
                        sum = IX + SP;
                        AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(IX ^ SP ^ sum) >> 8];
                        IX = sum;
                        break;

                    case 0x44:      /* LD B,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(BC, HIGH_REGISTER(IX));
                        break;

                    case 0x45:      /* LD B,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(BC, LOW_REGISTER(IX));
                        break;

                    case 0x46:      /* LD B,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_HIGH_REGISTER(BC, GetBYTE(adr));
                        break;

                    case 0x4c:      /* LD C,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(BC, HIGH_REGISTER(IX));
                        break;

                    case 0x4d:      /* LD C,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(BC, LOW_REGISTER(IX));
                        break;

                    case 0x4e:      /* LD C,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_LOW_REGISTER(BC, GetBYTE(adr));
                        break;

                    case 0x54:      /* LD D,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(DE, HIGH_REGISTER(IX));
                        break;

                    case 0x55:      /* LD D,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(DE, LOW_REGISTER(IX));
                        break;

                    case 0x56:      /* LD D,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_HIGH_REGISTER(DE, GetBYTE(adr));
                        break;

                    case 0x5c:      /* LD E,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(DE, HIGH_REGISTER(IX));
                        break;

                    case 0x5d:      /* LD E,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(DE, LOW_REGISTER(IX));
                        break;

                    case 0x5e:      /* LD E,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_LOW_REGISTER(DE, GetBYTE(adr));
                        break;

                    case 0x60:      /* LD IXH,B */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IX, HIGH_REGISTER(BC));
                        break;

                    case 0x61:      /* LD IXH,C */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IX, LOW_REGISTER(BC));
                        break;

                    case 0x62:      /* LD IXH,D */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IX, HIGH_REGISTER(DE));
                        break;

                    case 0x63:      /* LD IXH,E */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IX, LOW_REGISTER(DE));
                        break;

                    case 0x64:      /* LD IXH,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE; /* nop */
                        break;

                    case 0x65:      /* LD IXH,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IX, LOW_REGISTER(IX));
                        break;

                    case 0x66:      /* LD H,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_HIGH_REGISTER(HL, GetBYTE(adr));
                        break;

                    case 0x67:      /* LD IXH,A */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IX, HIGH_REGISTER(AF));
                        break;

                    case 0x68:      /* LD IXL,B */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IX, HIGH_REGISTER(BC));
                        break;

                    case 0x69:      /* LD IXL,C */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IX, LOW_REGISTER(BC));
                        break;

                    case 0x6a:      /* LD IXL,D */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IX, HIGH_REGISTER(DE));
                        break;

                    case 0x6b:      /* LD IXL,E */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IX, LOW_REGISTER(DE));
                        break;

                    case 0x6c:      /* LD IXL,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IX, HIGH_REGISTER(IX));
                        break;

                    case 0x6d:      /* LD IXL,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE; /* nop */
                        break;

                    case 0x6e:      /* LD L,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_LOW_REGISTER(HL, GetBYTE(adr));
                        break;

                    case 0x6f:      /* LD IXL,A */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IX, HIGH_REGISTER(AF));
                        break;

                    case 0x70:      /* LD (IX+dd),B */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, HIGH_REGISTER(BC));
                        break;

                    case 0x71:      /* LD (IX+dd),C */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, LOW_REGISTER(BC));
                        break;

                    case 0x72:      /* LD (IX+dd),D */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, HIGH_REGISTER(DE));
                        break;

                    case 0x73:      /* LD (IX+dd),E */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, LOW_REGISTER(DE));
                        break;

                    case 0x74:      /* LD (IX+dd),H */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, HIGH_REGISTER(HL));
                        break;

                    case 0x75:      /* LD (IX+dd),L */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, LOW_REGISTER(HL));
                        break;

                    case 0x77:      /* LD (IX+dd),A */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, HIGH_REGISTER(AF));
                        break;

                    case 0x7c:      /* LD A,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(AF, HIGH_REGISTER(IX));
                        break;

                    case 0x7d:      /* LD A,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(AF, LOW_REGISTER(IX));
                        break;

                    case 0x7e:      /* LD A,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_HIGH_REGISTER(AF, GetBYTE(adr));
                        break;

                    case 0x84:      /* ADD A,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = HIGH_REGISTER(IX);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp;
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x85:      /* ADD A,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IX);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp;
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x86:      /* ADD A,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp;
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x8c:      /* ADC A,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = HIGH_REGISTER(IX);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp + TSTFLAG(C);
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x8d:      /* ADC A,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IX);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp + TSTFLAG(C);
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x8e:      /* ADC A,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp + TSTFLAG(C);
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x96:      /* SUB (IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp;
                        AF = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0x94:      /* SUB IXH */
                        SETFLAG(C, 0);/* fall through, a bit less efficient but smaller code */

                    case 0x9c:      /* SBC A,IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = HIGH_REGISTER(IX);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp - TSTFLAG(C);
                        AF = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0x95:      /* SUB IXL */
                        SETFLAG(C, 0);/* fall through, a bit less efficient but smaller code */

                    case 0x9d:      /* SBC A,IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IX);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp - TSTFLAG(C);
                        AF = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0x9e:      /* SBC A,(IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp - TSTFLAG(C);
                        AF = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0xa4:      /* AND IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = andTable[((AF & IX) >> 8) & 0xff];
                        break;

                    case 0xa5:      /* AND IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = andTable[((AF >> 8) & IX) & 0xff];
                        break;

                    case 0xa6:      /* AND (IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        AF = andTable[((AF >> 8) & GetBYTE(adr)) & 0xff];
                        break;

                    case 0xac:      /* XOR IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = xororTable[((AF ^ IX) >> 8) & 0xff];
                        break;

                    case 0xad:      /* XOR IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = xororTable[((AF >> 8) ^ IX) & 0xff];
                        break;

                    case 0xae:      /* XOR (IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        AF = xororTable[((AF >> 8) ^ GetBYTE(adr)) & 0xff];
                        break;

                    case 0xb4:      /* OR IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = xororTable[((AF | IX) >> 8) & 0xff];
                        break;

                    case 0xb5:      /* OR IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = xororTable[((AF >> 8) | IX) & 0xff];
                        break;

                    case 0xb6:      /* OR (IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        AF = xororTable[((AF >> 8) | GetBYTE(adr)) & 0xff];
                        break;

                    case 0xbc:      /* CP IXH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = HIGH_REGISTER(IX);
                        AF = (AF & ~0x28) | (temp & 0x28);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp;
                        AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                            cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0xbd:      /* CP IXL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IX);
                        AF = (AF & ~0x28) | (temp & 0x28);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp;
                        AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                            cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0xbe:      /* CP (IX+dd) */
                        tStates += 19;
                        adr = IX + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        AF = (AF & ~0x28) | (temp & 0x28);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp;
                        AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                            cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0xcb:      /* CB prefix */
                        adr = IX + (int8) RAM_PP(PC);
                        switch ((op = GetBYTE(PC)) & 7) {

                            case 0:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = HIGH_REGISTER(BC);
                                break;

                            case 1:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = LOW_REGISTER(BC);
                                break;

                            case 2:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = HIGH_REGISTER(DE);
                                break;

                            case 3:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = LOW_REGISTER(DE);
                                break;

                            case 4:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = HIGH_REGISTER(HL);
                                break;

                            case 5:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = LOW_REGISTER(HL);
                                break;

                            case 6:
                                CHECK_BREAK_BYTE(adr);
                                ++PC;
                                acu = GetBYTE(adr);
                                break;

                            case 7:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = HIGH_REGISTER(AF);
                                break;
                        }
                        switch (op & 0xc0) {

                            case 0x00:  /* shift/rotate */
                                tStates += 23;
                                switch (op & 0x38) {

                                    case 0x00:  /* RLC */
                                        temp = (acu << 1) | (acu >> 7);
                                        cbits = temp & 1;
                                        goto cbshflg2;

                                    case 0x08:  /* RRC */
                                        temp = (acu >> 1) | (acu << 7);
                                        cbits = temp & 0x80;
                                        goto cbshflg2;

                                    case 0x10:  /* RL */
                                        temp = (acu << 1) | TSTFLAG(C);
                                        cbits = acu & 0x80;
                                        goto cbshflg2;

                                    case 0x18:  /* RR */
                                        temp = (acu >> 1) | (TSTFLAG(C) << 7);
                                        cbits = acu & 1;
                                        goto cbshflg2;

                                    case 0x20:  /* SLA */
                                        temp = acu << 1;
                                        cbits = acu & 0x80;
                                        goto cbshflg2;

                                    case 0x28:  /* SRA */
                                        temp = (acu >> 1) | (acu & 0x80);
                                        cbits = acu & 1;
                                        goto cbshflg2;

                                    case 0x30:  /* SLIA */
                                        temp = (acu << 1) | 1;
                                        cbits = acu & 0x80;
                                        goto cbshflg2;

                                    case 0x38:  /* SRL */
                                        temp = acu >> 1;
                                        cbits = acu & 1;
                                        cbshflg2:
                                        AF = (AF & ~0xff) | rotateShiftTable[temp & 0xff] | !!cbits;
                                        /* !!cbits == 0 if cbits == 0 !!cbits == 1 if cbits > 0 */
                                }
                                break;

                            case 0x40:  /* BIT */
                                tStates += 20;
                                if (acu & (1 << ((op >> 3) & 7)))
                                    AF = (AF & ~0xfe) | 0x10 | (((op & 0x38) == 0x38) << 7);
                                else
                                    AF = (AF & ~0xfe) | 0x54;
                                if ((op & 7) != 6)
                                    AF |= (acu & 0x28);
                                temp = acu;
                                break;

                            case 0x80:  /* RES */
                                tStates += 23;
                                temp = acu & ~(1 << ((op >> 3) & 7));
                                break;

                            case 0xc0:  /* SET */
                                tStates += 23;
                                temp = acu | (1 << ((op >> 3) & 7));
                                break;
                        }
                        switch (op & 7) {

                            case 0:
                                SET_HIGH_REGISTER(BC, temp);
                                break;

                            case 1:
                                SET_LOW_REGISTER(BC, temp);
                                break;

                            case 2:
                               SET_HIGH_REGISTER(DE, temp);
                                break;

                            case 3:
                                SET_LOW_REGISTER(DE, temp);
                                break;

                            case 4:
                                SET_HIGH_REGISTER(HL, temp);
                                break;

                            case 5:
                                SET_LOW_REGISTER(HL, temp);
                                break;

                            case 6:
                                PutBYTE(adr, temp);
                                break;

                            case 7:
                                SET_HIGH_REGISTER(AF, temp);
                                break;
                        }
                        break;

                    case 0xe1:      /* POP IX */
                        tStates += 14;
                        CHECK_BREAK_WORD(SP);
                        POP(IX);
                        break;

                    case 0xe3:      /* EX (SP),IX */
                        tStates += 23;
                        CHECK_BREAK_WORD(SP);
                        temp = IX;
                        POP(IX);
                        PUSH(temp);
                        break;

                    case 0xe5:      /* PUSH IX */
                        tStates += 15;
                        CHECK_BREAK_WORD(SP - 2);
                        PUSH(IX);
                        break;

                    case 0xe9:      /* JP (IX) */
                        tStates += 8;
                        sim_brk_pend[0] = FALSE;
                        PCQ_ENTRY(PCX);
                        PC = IX;
                        break;

                    case 0xf9:      /* LD SP,IX */
                        tStates += 10;
                        sim_brk_pend[0] = FALSE;
                        SP = IX;
                        break;

                    default:        /* ignore DD */
                        sim_brk_pend[0] = FALSE;
                        CHECK_CPU_Z80;
                        PC--;
                }
                break;

            case 0xde:      /* SBC A,nn */
                tStates += 7; /* SBI nn 7 */
                sim_brk_pend[0] = FALSE;
                temp = RAM_PP(PC);
                acu = HIGH_REGISTER(AF);
                sum = acu - temp - TSTFLAG(C);
                cbits = acu ^ temp ^ sum;
                AF = subTable[sum & 0xff] | cbitsTable[cbits & 0x1ff] | (SET_PV);
                break;

            case 0xdf:      /* RST 18H */
                tStates += 11; /* RST 3 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(PC);
                PCQ_ENTRY(PCX);
                PC = 0x18;
                break;

            case 0xe0:      /* RET PO */
                if (TSTFLAG(P)) {
                    sim_brk_pend[0] = FALSE;
                    tStates += 5; /* RPO 5 */
                }
                else {
                    CHECK_BREAK_WORD(SP);
                    PCQ_ENTRY(PCX);
                    POP(PC);
                    tStates += 11; /* RPO 11 */
                }
                break;

            case 0xe1:      /* POP HL */
                tStates += 10; /* POP H 10 */
                CHECK_BREAK_WORD(SP);
                POP(HL);
                break;

            case 0xe2:      /* JP PO,nnnn */
                sim_brk_pend[0] = FALSE;
                JPC(!TSTFLAG(P));       /* also updates tStates */
                break;

            case 0xe3:      /* EX (SP),HL */
                tStates += (chiptype == CHIP_TYPE_8080 ? 18 : 19); /* XTHL 18 */
                CHECK_BREAK_WORD(SP);
                temp = HL;
                POP(HL);
                PUSH(temp);
                break;

            case 0xe4:      /* CALL PO,nnnn */
                CALLC(!TSTFLAG(P));     /* also updates tStates */
                break;

            case 0xe5:      /* PUSH HL */
                tStates += 11; /* PUSH H 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(HL);
                break;

            case 0xe6:      /* AND nn */
                tStates += 7; /* ANI nn 7 */
                sim_brk_pend[0] = FALSE;
                AF = andTable[((AF >> 8) & RAM_PP(PC)) & 0xff];
                break;

            case 0xe7:      /* RST 20H */
                tStates += 11; /* RST 4 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(PC);
                PCQ_ENTRY(PCX);
                PC = 0x20;
                break;

            case 0xe8:      /* RET PE */
                if (TSTFLAG(P)) {
                    CHECK_BREAK_WORD(SP);
                    PCQ_ENTRY(PCX);
                    POP(PC);
                    tStates += 11; /* RPE 11 */
                }
                else {
                    sim_brk_pend[0] = FALSE;
                    tStates += 5; /* RPE 5 */
                }
                break;

            case 0xe9:      /* JP (HL) */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* PCHL 5 */
                sim_brk_pend[0] = FALSE;
                PCQ_ENTRY(PCX);
                PC = HL;
                break;

            case 0xea:      /* JP PE,nnnn */
                sim_brk_pend[0] = FALSE;
                JPC(TSTFLAG(P));        /* also updates tStates */
                break;

            case 0xeb:      /* EX DE,HL */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 4); /* XCHG 5 */
                sim_brk_pend[0] = FALSE;
                temp = HL;
                HL = DE;
                DE = temp;
                break;

            case 0xec:      /* CALL PE,nnnn */
                CALLC(TSTFLAG(P));      /* also updates tStates */
                break;

            case 0xed:      /* ED prefix */
                if (chiptype == CHIP_TYPE_8080) {
                    if (cpu_unit.flags & UNIT_CPU_OPSTOP) {
                        reason = STOP_OPCODE;
                        goto end_decode;
                    }
                    else {
                        CALLC(1);       /* also updates tStates */
                        break;
                    }
                }
                switch (RAM_PP(PC)) {

                    case 0x40:      /* IN B,(C) */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        temp = in(LOW_REGISTER(BC));
                        SET_HIGH_REGISTER(BC, temp);
                        AF = (AF & ~0xfe) | rotateShiftTable[temp & 0xff];
                        break;

                    case 0x41:      /* OUT (C),B */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        out(LOW_REGISTER(BC), HIGH_REGISTER(BC));
                        break;

                    case 0x42:      /* SBC HL,BC */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        HL &= ADDRMASK;
                        BC &= ADDRMASK;
                        sum = HL - BC - TSTFLAG(C);
                        AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                            cbits2Z80Table[((HL ^ BC ^ sum) >> 8) & 0x1ff];
                        HL = sum;
                        break;

                    case 0x43:      /* LD (nnnn),BC */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        PutWORD(temp, BC);
                        PC += 2;
                        break;

                    case 0x44:      /* NEG */

                    case 0x4C:      /* NEG, unofficial */

                    case 0x54:      /* NEG, unofficial */

                    case 0x5C:      /* NEG, unofficial */

                    case 0x64:      /* NEG, unofficial */

                    case 0x6C:      /* NEG, unofficial */

                    case 0x74:      /* NEG, unofficial */

                    case 0x7C:      /* NEG, unofficial */
                        tStates += 8;
                        sim_brk_pend[0] = FALSE;
                        temp = HIGH_REGISTER(AF);
                        AF = ((~(AF & 0xff00) + 1) & 0xff00); /* AF = (-(AF & 0xff00) & 0xff00); */
                        AF |= ((AF >> 8) & 0xa8) | (((AF & 0xff00) == 0) << 6) | negTable[temp];
                        break;

                    case 0x45:      /* RETN */

                    case 0x55:      /* RETN, unofficial */

                    case 0x5D:      /* RETN, unofficial */

                    case 0x65:      /* RETN, unofficial */

                    case 0x6D:      /* RETN, unofficial */

                    case 0x75:      /* RETN, unofficial */

                    case 0x7D:      /* RETN, unofficial */
                        tStates += 14;
                        IFF_S |= IFF_S >> 1;
                        CHECK_BREAK_WORD(SP);
                        PCQ_ENTRY(PCX);
                        POP(PC);
                        break;

                    case 0x46:      /* IM 0 */
                        tStates += 8;
                        sim_brk_pend[0] = FALSE;
                        /* interrupt mode 0 */
                        break;

                    case 0x47:      /* LD I,A */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        IR_S = (IR_S & 0xff) | (AF & ~0xff);
                        break;

                    case 0x48:      /* IN C,(C) */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        temp = in(LOW_REGISTER(BC));
                        SET_LOW_REGISTER(BC, temp);
                        AF = (AF & ~0xfe) | rotateShiftTable[temp & 0xff];
                        break;

                    case 0x49:      /* OUT (C),C */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        out(LOW_REGISTER(BC), LOW_REGISTER(BC));
                        break;

                    case 0x4a:      /* ADC HL,BC */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        HL &= ADDRMASK;
                        BC &= ADDRMASK;
                        sum = HL + BC + TSTFLAG(C);
                        AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                            cbitsZ80Table[(HL ^ BC ^ sum) >> 8];
                        HL = sum;
                        break;

                    case 0x4b:      /* LD BC,(nnnn) */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        BC = GET_WORD(temp);
                        PC += 2;
                        break;

                    case 0x4d:      /* RETI */
                        tStates += 14;
                        IFF_S |= IFF_S >> 1;
                        CHECK_BREAK_WORD(SP);
                        PCQ_ENTRY(PCX);
                        POP(PC);
                        break;

                    case 0x4f:      /* LD R,A */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        IR_S = (IR_S & ~0xff) | ((AF >> 8) & 0xff);
                        break;

                    case 0x50:      /* IN D,(C) */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        temp = in(LOW_REGISTER(BC));
                        SET_HIGH_REGISTER(DE, temp);
                        AF = (AF & ~0xfe) | rotateShiftTable[temp & 0xff];
                        break;

                    case 0x51:      /* OUT (C),D */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        out(LOW_REGISTER(BC), HIGH_REGISTER(DE));
                        break;

                    case 0x52:      /* SBC HL,DE */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        HL &= ADDRMASK;
                        DE &= ADDRMASK;
                        sum = HL - DE - TSTFLAG(C);
                        AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                            cbits2Z80Table[((HL ^ DE ^ sum) >> 8) & 0x1ff];
                        HL = sum;
                        break;

                    case 0x53:      /* LD (nnnn),DE */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        PutWORD(temp, DE);
                        PC += 2;
                        break;

                    case 0x56:      /* IM 1 */
                        tStates += 8;
                        sim_brk_pend[0] = FALSE;
                        /* interrupt mode 1 */
                        break;

                    case 0x57:      /* LD A,I */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = (AF & 0x29) | (IR_S & ~0xff) | ((IR_S >> 8) & 0x80) | (((IR_S & ~0xff) == 0) << 6) | ((IFF_S & 2) << 1);
                        break;

                    case 0x58:      /* IN E,(C) */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        temp = in(LOW_REGISTER(BC));
                        SET_LOW_REGISTER(DE, temp);
                        AF = (AF & ~0xfe) | rotateShiftTable[temp & 0xff];
                        break;

                    case 0x59:      /* OUT (C),E */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        out(LOW_REGISTER(BC), LOW_REGISTER(DE));
                        break;

                    case 0x5a:      /* ADC HL,DE */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        HL &= ADDRMASK;
                        DE &= ADDRMASK;
                        sum = HL + DE + TSTFLAG(C);
                        AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                            cbitsZ80Table[(HL ^ DE ^ sum) >> 8];
                        HL = sum;
                        break;

                    case 0x5b:      /* LD DE,(nnnn) */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        DE = GET_WORD(temp);
                        PC += 2;
                        break;

                    case 0x5e:      /* IM 2 */
                        tStates += 8;
                        sim_brk_pend[0] = FALSE;
                        /* interrupt mode 2 */
                        break;

                    case 0x5f:      /* LD A,R */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = (AF & 0x29) | ((IR_S & 0xff) << 8) | (IR_S & 0x80) |
                            (((IR_S & 0xff) == 0) << 6) | ((IFF_S & 2) << 1);
                        break;

                    case 0x60:      /* IN H,(C) */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        temp = in(LOW_REGISTER(BC));
                        SET_HIGH_REGISTER(HL, temp);
                        AF = (AF & ~0xfe) | rotateShiftTable[temp & 0xff];
                        break;

                    case 0x61:      /* OUT (C),H */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        out(LOW_REGISTER(BC), HIGH_REGISTER(HL));
                        break;

                    case 0x62:      /* SBC HL,HL */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        HL &= ADDRMASK;
                        sum = HL - HL - TSTFLAG(C);
                        AF = (AF & ~0xff) | (((sum & ADDRMASK) == 0) << 6) |
                            cbits2Z80DupTable[(sum >> 8) & 0x1ff];
                        HL = sum;
                        break;

                    case 0x63:      /* LD (nnnn),HL */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        PutWORD(temp, HL);
                        PC += 2;
                        break;

                    case 0x67:      /* RRD */
                        tStates += 18;
                        sim_brk_pend[0] = FALSE;
                        temp = GetBYTE(HL);
                        acu = HIGH_REGISTER(AF);
                        PutBYTE(HL, HIGH_DIGIT(temp) | (LOW_DIGIT(acu) << 4));
                        AF = rrdrldTable[(acu & 0xf0) | LOW_DIGIT(temp)] | (AF & 1);
                        break;

                    case 0x68:      /* IN L,(C) */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        temp = in(LOW_REGISTER(BC));
                        SET_LOW_REGISTER(HL, temp);
                        AF = (AF & ~0xfe) | rotateShiftTable[temp & 0xff];
                        break;

                    case 0x69:      /* OUT (C),L */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        out(LOW_REGISTER(BC), LOW_REGISTER(HL));
                        break;

                    case 0x6a:      /* ADC HL,HL */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        HL &= ADDRMASK;
                        sum = HL + HL + TSTFLAG(C);
                        AF = (AF & ~0xff) | (((sum & ADDRMASK) == 0) << 6) |
                            cbitsZ80DupTable[sum >> 8];
                        HL = sum;
                        break;

                    case 0x6b:      /* LD HL,(nnnn) */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        HL = GET_WORD(temp);
                        PC += 2;
                        break;

                    case 0x6f:      /* RLD */
                        tStates += 18;
                        sim_brk_pend[0] = FALSE;
                        temp = GetBYTE(HL);
                        acu = HIGH_REGISTER(AF);
                        PutBYTE(HL, (LOW_DIGIT(temp) << 4) | LOW_DIGIT(acu));
                        AF = rrdrldTable[(acu & 0xf0) | HIGH_DIGIT(temp)] | (AF & 1);
                        break;

                    case 0x70:      /* IN (C) */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        temp = in(LOW_REGISTER(BC));
                        SET_LOW_REGISTER(temp, temp);
                        AF = (AF & ~0xfe) | rotateShiftTable[temp & 0xff];
                        break;

                    case 0x71:      /* OUT (C),0 */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        out(LOW_REGISTER(BC), 0);
                        break;

                    case 0x72:      /* SBC HL,SP */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        HL &= ADDRMASK;
                        SP &= ADDRMASK;
                        sum = HL - SP - TSTFLAG(C);
                        AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                            cbits2Z80Table[((HL ^ SP ^ sum) >> 8) & 0x1ff];
                        HL = sum;
                        break;

                    case 0x73:      /* LD (nnnn),SP */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        PutWORD(temp, SP);
                        PC += 2;
                        break;

                    case 0x78:      /* IN A,(C) */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        temp = in(LOW_REGISTER(BC));
                        SET_HIGH_REGISTER(AF, temp);
                        AF = (AF & ~0xfe) | rotateShiftTable[temp & 0xff];
                        break;

                    case 0x79:      /* OUT (C),A */
                        tStates += 12;
                        sim_brk_pend[0] = FALSE;
                        out(LOW_REGISTER(BC), HIGH_REGISTER(AF));
                        break;

                    case 0x7a:      /* ADC HL,SP */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        HL &= ADDRMASK;
                        SP &= ADDRMASK;
                        sum = HL + SP + TSTFLAG(C);
                        AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) | (((sum & ADDRMASK) == 0) << 6) |
                            cbitsZ80Table[(HL ^ SP ^ sum) >> 8];
                        HL = sum;
                        break;

                    case 0x7b:      /* LD SP,(nnnn) */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        SP = GET_WORD(temp);
                        PC += 2;
                        break;

                    case 0xa0:      /* LDI */
                        tStates += 16;
                        CHECK_BREAK_TWO_BYTES(HL, DE);
                        acu = RAM_PP(HL);
                        PUT_BYTE_PP(DE, acu);
                        acu += HIGH_REGISTER(AF);
                        AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
                            (((--BC & ADDRMASK) != 0) << 2);
                        break;

                    case 0xa1:      /* CPI */
                        tStates += 16;
                        CHECK_BREAK_BYTE(HL);
                        acu = HIGH_REGISTER(AF);
                        temp = RAM_PP(HL);
                        sum = acu - temp;
                        cbits = acu ^ temp ^ sum;
                        AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
                            (((sum - ((cbits & 16) >> 4)) & 2) << 4) | (cbits & 16) |
                            ((sum - ((cbits >> 4) & 1)) & 8) |
                            ((--BC & ADDRMASK) != 0) << 2 | 2;
                        if ((sum & 15) == 8 && (cbits & 16) != 0)
                            AF &= ~8;
                        break;

/*  SF, ZF, YF, XF flags are affected by decreasing register B, as in DEC B.
    NF flag A is copy of bit 7 of the value read from or written to an I/O port.
    INI/INIR/IND/INDR use the C flag in stead of the L register. There is a
    catch though, because not the value of C is used, but C + 1 if it's INI/INIR or
    C - 1 if it's IND/INDR. So, first of all INI/INIR:
        HF and CF Both set if ((HL) + ((C + 1) & 255) > 255)
        PF The parity of (((HL) + ((C + 1) & 255)) & 7) xor B)                      */
                    case 0xa2:      /* INI */
                        tStates += 16;
                        CHECK_BREAK_BYTE(HL);
                        acu = in(LOW_REGISTER(BC));
                        PutBYTE(HL, acu);
                        ++HL;
                        temp = HIGH_REGISTER(BC);
                        BC -= 0x100;
                        INOUTFLAGS_NONZERO((LOW_REGISTER(BC) + 1) & 0xff);
                        break;

/*  SF, ZF, YF, XF flags are affected by decreasing register B, as in DEC B.
    NF flag A is copy of bit 7 of the value read from or written to an I/O port.
    And now the for OUTI/OTIR/OUTD/OTDR instructions. Take state of the L
    after the increment or decrement of HL; add the value written to the I/O port
    to; call that k for now. If k > 255, then the CF and HF flags are set. The PF
    flags is set like the parity of k bitwise and'ed with 7, bitwise xor'ed with B.
    HF and CF Both set if ((HL) + L > 255)
    PF The parity of ((((HL) + L) & 7) xor B)                                       */
                    case 0xa3:      /* OUTI */
                        tStates += 16;
                        CHECK_BREAK_BYTE(HL);
                        acu = GetBYTE(HL);
                        out(LOW_REGISTER(BC), acu);
                        ++HL;
                        temp = HIGH_REGISTER(BC);
                        BC -= 0x100;
                        INOUTFLAGS_NONZERO(LOW_REGISTER(HL));
                        break;

                    case 0xa8:      /* LDD */
                        tStates += 16;
                        CHECK_BREAK_TWO_BYTES(HL, DE);
                        acu = RAM_MM(HL);
                        PUT_BYTE_MM(DE, acu);
                        acu += HIGH_REGISTER(AF);
                        AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
                            (((--BC & ADDRMASK) != 0) << 2);
                        break;

                    case 0xa9:      /* CPD */
                        tStates += 16;
                        CHECK_BREAK_BYTE(HL);
                        acu = HIGH_REGISTER(AF);
                        temp = RAM_MM(HL);
                        sum = acu - temp;
                        cbits = acu ^ temp ^ sum;
                        AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
                            (((sum - ((cbits & 16) >> 4)) & 2) << 4) | (cbits & 16) |
                            ((sum - ((cbits >> 4) & 1)) & 8) |
                            ((--BC & ADDRMASK) != 0) << 2 | 2;
                        if ((sum & 15) == 8 && (cbits & 16) != 0)
                            AF &= ~8;
                        break;

/*  SF, ZF, YF, XF flags are affected by decreasing register B, as in DEC B.
    NF flag A is copy of bit 7 of the value read from or written to an I/O port.
    INI/INIR/IND/INDR use the C flag in stead of the L register. There is a
    catch though, because not the value of C is used, but C + 1 if it's INI/INIR or
    C - 1 if it's IND/INDR. And last IND/INDR:
        HF and CF Both set if ((HL) + ((C - 1) & 255) > 255)
        PF The parity of (((HL) + ((C - 1) & 255)) & 7) xor B)                      */
                    case 0xaa:      /* IND */
                        tStates += 16;
                        CHECK_BREAK_BYTE(HL);
                        acu = in(LOW_REGISTER(BC));
                        PutBYTE(HL, acu);
                        --HL;
                        temp = HIGH_REGISTER(BC);
                        BC -= 0x100;
                        INOUTFLAGS_NONZERO((LOW_REGISTER(BC) - 1) & 0xff);
                        break;

                    case 0xab:      /* OUTD */
                        tStates += 16;
                        CHECK_BREAK_BYTE(HL);
                        acu = GetBYTE(HL);
                        out(LOW_REGISTER(BC), acu);
                        --HL;
                        temp = HIGH_REGISTER(BC);
                        BC -= 0x100;
                        INOUTFLAGS_NONZERO(LOW_REGISTER(HL));
                        break;

                    case 0xb0:      /* LDIR */
                        tStates -= 5;
                        BC &= ADDRMASK;
                        if (BC == 0)
                            BC = 0x10000;
                        do {
                            tStates += 21;
                            CHECK_BREAK_TWO_BYTES(HL, DE);
                            acu = RAM_PP(HL);
                            PUT_BYTE_PP(DE, acu);
                        } while (--BC);
                        acu += HIGH_REGISTER(AF);
                        AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
                        break;

                    case 0xb1:      /* CPIR */
                        tStates -= 5;
                        acu = HIGH_REGISTER(AF);
                        BC &= ADDRMASK;
                        if (BC == 0)
                            BC = 0x10000;
                        do {
                            tStates += 21;
                            CHECK_BREAK_BYTE(HL);
                            temp = RAM_PP(HL);
                            op = --BC != 0;
                            sum = acu - temp;
                        } while (op && sum != 0);
                        cbits = acu ^ temp ^ sum;
                        AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
                            (((sum - ((cbits & 16) >> 4)) & 2) << 4) |
                            (cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
                            op << 2 | 2;
                        if ((sum & 15) == 8 && (cbits & 16) != 0)
                            AF &= ~8;
                        break;

                    case 0xb2:      /* INIR */
                        tStates -= 5;
                        temp = HIGH_REGISTER(BC);
                        if (temp == 0)
                            temp = 0x100;
                        do {
                            tStates += 21;
                            CHECK_BREAK_BYTE(HL);
                            acu = in(LOW_REGISTER(BC));
                            PutBYTE(HL, acu);
                            ++HL;
                        } while (--temp);
                        temp = HIGH_REGISTER(BC);
                        SET_HIGH_REGISTER(BC, 0);
                        INOUTFLAGS_ZERO((LOW_REGISTER(BC) + 1) & 0xff);
                        break;

                    case 0xb3:      /* OTIR */
                        tStates -= 5;
                        temp = HIGH_REGISTER(BC);
                        if (temp == 0)
                            temp = 0x100;
                        do {
                            tStates += 21;
                            CHECK_BREAK_BYTE(HL);
                            acu = GetBYTE(HL);
                            out(LOW_REGISTER(BC), acu);
                            ++HL;
                        } while (--temp);
                        temp = HIGH_REGISTER(BC);
                        SET_HIGH_REGISTER(BC, 0);
                        INOUTFLAGS_ZERO(LOW_REGISTER(HL));
                        break;

                    case 0xb8:      /* LDDR */
                        tStates -= 5;
                        BC &= ADDRMASK;
                        if (BC == 0)
                            BC = 0x10000;
                        do {
                            tStates += 21;
                            CHECK_BREAK_TWO_BYTES(HL, DE);
                            acu = RAM_MM(HL);
                            PUT_BYTE_MM(DE, acu);
                        } while (--BC);
                        acu += HIGH_REGISTER(AF);
                        AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
                        break;

                    case 0xb9:      /* CPDR */
                        tStates -= 5;
                        acu = HIGH_REGISTER(AF);
                        BC &= ADDRMASK;
                        if (BC == 0)
                            BC = 0x10000;
                        do {
                            tStates += 21;
                            CHECK_BREAK_BYTE(HL);
                            temp = RAM_MM(HL);
                            op = --BC != 0;
                            sum = acu - temp;
                        } while (op && sum != 0);
                        cbits = acu ^ temp ^ sum;
                        AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
                            (((sum - ((cbits & 16) >> 4)) & 2) << 4) |
                            (cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
                            op << 2 | 2;
                        if ((sum & 15) == 8 && (cbits & 16) != 0)
                            AF &= ~8;
                        break;

                    case 0xba:      /* INDR */
                        tStates -= 5;
                        temp = HIGH_REGISTER(BC);
                        if (temp == 0)
                            temp = 0x100;
                        do {
                            tStates += 21;
                            CHECK_BREAK_BYTE(HL);
                            acu = in(LOW_REGISTER(BC));
                            PutBYTE(HL, acu);
                            --HL;
                        } while (--temp);
                        temp = HIGH_REGISTER(BC);
                        SET_HIGH_REGISTER(BC, 0);
                        INOUTFLAGS_ZERO((LOW_REGISTER(BC) - 1) & 0xff);
                        break;

                    case 0xbb:      /* OTDR */
                        tStates -= 5;
                        temp = HIGH_REGISTER(BC);
                        if (temp == 0)
                            temp = 0x100;
                        do {
                            tStates += 21;
                            CHECK_BREAK_BYTE(HL);
                            acu = GetBYTE(HL);
                            out(LOW_REGISTER(BC), acu);
                            --HL;
                        } while (--temp);
                        temp = HIGH_REGISTER(BC);
                        SET_HIGH_REGISTER(BC, 0);
                        INOUTFLAGS_ZERO(LOW_REGISTER(HL));
                        break;

                    default:        /* ignore ED and following byte */
                        sim_brk_pend[0] = FALSE;
                        CHECK_CPU_Z80;
                }
                break;

            case 0xee:      /* XOR nn */
                tStates += 7; /* XRI nn 7 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF >> 8) ^ RAM_PP(PC)) & 0xff];
                break;

            case 0xef:      /* RST 28H */
                tStates += 11; /* RST 5 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(PC);
                PCQ_ENTRY(PCX);
                PC = 0x28;
                break;

            case 0xf0:      /* RET P */
                if (TSTFLAG(S)) {
                    sim_brk_pend[0] = FALSE;
                    tStates += 5; /* RP 5 */
                }
                else {
                    CHECK_BREAK_WORD(SP);
                    PCQ_ENTRY(PCX);
                    POP(PC);
                    tStates += 11; /* RP 11 */
                }
                break;

            case 0xf1:      /* POP AF */
                tStates += 10; /* POP PSW 10 */
                CHECK_BREAK_WORD(SP);
                POP(AF);
                break;

            case 0xf2:      /* JP P,nnnn */
                sim_brk_pend[0] = FALSE;
                JPC(!TSTFLAG(S));       /* also updates tStates */
                break;

            case 0xf3:      /* DI */
                tStates += 4; /* DI 4 */
                sim_brk_pend[0] = FALSE;
                IFF_S = 0;
                break;

            case 0xf4:      /* CALL P,nnnn */
                CALLC(!TSTFLAG(S));     /* also updates tStates */
                break;

            case 0xf5:      /* PUSH AF */
                tStates += 11; /* PUSH PSW 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(AF);
                break;

            case 0xf6:      /* OR nn */
                tStates += 7; /* ORI nn 7 */
                sim_brk_pend[0] = FALSE;
                AF = xororTable[((AF >> 8) | RAM_PP(PC)) & 0xff];
                break;

            case 0xf7:      /* RST 30H */
                tStates += 11; /* RST 6 11 */
                CHECK_BREAK_WORD(SP - 2);
                PUSH(PC);
                PCQ_ENTRY(PCX);
                PC = 0x30;
                break;

            case 0xf8:      /* RET M */
                if (TSTFLAG(S)) {
                    CHECK_BREAK_WORD(SP);
                    PCQ_ENTRY(PCX);
                    POP(PC);
                    tStates += 11; /* RM 11 */
                }
                else {
                    sim_brk_pend[0] = FALSE;
                    tStates += 5; /* RM 5 */
                }
                break;

            case 0xf9:      /* LD SP,HL */
                tStates += (chiptype == CHIP_TYPE_8080 ? 5 : 6); /* SPHL 5 */
                sim_brk_pend[0] = FALSE;
                SP = HL;
                break;

            case 0xfa:      /* JP M,nnnn */
                sim_brk_pend[0] = FALSE;
                JPC(TSTFLAG(S));        /* also updates tStates */
                break;

            case 0xfb:      /* EI */
                tStates += 4; /* EI 4 */
                sim_brk_pend[0] = FALSE;
                IFF_S = 3;
                break;

            case 0xfc:      /* CALL M,nnnn */
                CALLC(TSTFLAG(S));      /* also updates tStates */
                break;

            case 0xfd:      /* FD prefix */
                if (chiptype == CHIP_TYPE_8080) {
                    if (cpu_unit.flags & UNIT_CPU_OPSTOP) {
                        reason = STOP_OPCODE;
                        goto end_decode;
                    }
                    else {
                        CALLC(1);       /* also updates tStates */
                        break;
                    }
                }
                switch (RAM_PP(PC)) {

                    case 0x09:      /* ADD IY,BC */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        IY &= ADDRMASK;
                        BC &= ADDRMASK;
                        sum = IY + BC;
                        AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(IY ^ BC ^ sum) >> 8];
                        IY = sum;
                        break;

                    case 0x19:      /* ADD IY,DE */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        IY &= ADDRMASK;
                        DE &= ADDRMASK;
                        sum = IY + DE;
                        AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(IY ^ DE ^ sum) >> 8];
                        IY = sum;
                        break;

                    case 0x21:      /* LD IY,nnnn */
                        tStates += 14;
                        sim_brk_pend[0] = FALSE;
                        IY = GET_WORD(PC);
                        PC += 2;
                        break;

                    case 0x22:      /* LD (nnnn),IY */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        PutWORD(temp, IY);
                        PC += 2;
                        break;

                    case 0x23:      /* INC IY */
                        tStates += 10;
                        sim_brk_pend[0] = FALSE;
                        ++IY;
                        break;

                    case 0x24:      /* INC IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        IY += 0x100;
                        AF = (AF & ~0xfe) | incZ80Table[HIGH_REGISTER(IY)];
                        break;

                    case 0x25:      /* DEC IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        IY -= 0x100;
                        AF = (AF & ~0xfe) | decZ80Table[HIGH_REGISTER(IY)];
                        break;

                    case 0x26:      /* LD IYH,nn */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IY, RAM_PP(PC));
                        break;

                    case 0x29:      /* ADD IY,IY */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        IY &= ADDRMASK;
                        sum = IY + IY;
                        AF = (AF & ~0x3b) | cbitsDup16Table[sum >> 8];
                        IY = sum;
                        break;

                    case 0x2a:      /* LD IY,(nnnn) */
                        tStates += 20;
                        temp = GET_WORD(PC);
                        CHECK_BREAK_WORD(temp);
                        IY = GET_WORD(temp);
                        PC += 2;
                        break;

                    case 0x2b:      /* DEC IY */
                        tStates += 10;
                        sim_brk_pend[0] = FALSE;
                        --IY;
                        break;

                    case 0x2c:      /* INC IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IY) + 1;
                        SET_LOW_REGISTER(IY, temp);
                        AF = (AF & ~0xfe) | incZ80Table[temp];
                        break;

                    case 0x2d:      /* DEC IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IY) - 1;
                        SET_LOW_REGISTER(IY, temp);
                        AF = (AF & ~0xfe) | decZ80Table[temp & 0xff];
                        break;

                    case 0x2e:      /* LD IYL,nn */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IY, RAM_PP(PC));
                        break;

                    case 0x34:      /* INC (IY+dd) */
                        tStates += 23;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr) + 1;
                        PutBYTE(adr, temp);
                        AF = (AF & ~0xfe) | incZ80Table[temp];
                        break;

                    case 0x35:      /* DEC (IY+dd) */
                        tStates += 23;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr) - 1;
                        PutBYTE(adr, temp);
                        AF = (AF & ~0xfe) | decZ80Table[temp & 0xff];
                        break;

                    case 0x36:      /* LD (IY+dd),nn */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, RAM_PP(PC));
                        break;

                    case 0x39:      /* ADD IY,SP */
                        tStates += 15;
                        sim_brk_pend[0] = FALSE;
                        IY &= ADDRMASK;
                        SP &= ADDRMASK;
                        sum = IY + SP;
                        AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) | cbitsTable[(IY ^ SP ^ sum) >> 8];
                        IY = sum;
                        break;

                    case 0x44:      /* LD B,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(BC, HIGH_REGISTER(IY));
                        break;

                    case 0x45:      /* LD B,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(BC, LOW_REGISTER(IY));
                        break;

                    case 0x46:      /* LD B,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_HIGH_REGISTER(BC, GetBYTE(adr));
                        break;

                    case 0x4c:      /* LD C,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(BC, HIGH_REGISTER(IY));
                        break;

                    case 0x4d:      /* LD C,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(BC, LOW_REGISTER(IY));
                        break;

                    case 0x4e:      /* LD C,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_LOW_REGISTER(BC, GetBYTE(adr));
                        break;

                    case 0x54:      /* LD D,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(DE, HIGH_REGISTER(IY));
                        break;

                    case 0x55:      /* LD D,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(DE, LOW_REGISTER(IY));
                        break;

                    case 0x56:      /* LD D,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_HIGH_REGISTER(DE, GetBYTE(adr));
                        break;

                    case 0x5c:      /* LD E,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(DE, HIGH_REGISTER(IY));
                        break;

                    case 0x5d:      /* LD E,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(DE, LOW_REGISTER(IY));
                        break;

                    case 0x5e:      /* LD E,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_LOW_REGISTER(DE, GetBYTE(adr));
                        break;

                    case 0x60:      /* LD IYH,B */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IY, HIGH_REGISTER(BC));
                        break;

                    case 0x61:      /* LD IYH,C */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IY, LOW_REGISTER(BC));
                        break;

                    case 0x62:      /* LD IYH,D */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IY, HIGH_REGISTER(DE));
                        break;

                    case 0x63:      /* LD IYH,E */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IY, LOW_REGISTER(DE));
                        break;

                    case 0x64:      /* LD IYH,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE; /* nop */
                        break;

                    case 0x65:      /* LD IYH,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IY, LOW_REGISTER(IY));
                        break;

                    case 0x66:      /* LD H,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_HIGH_REGISTER(HL, GetBYTE(adr));
                        break;

                    case 0x67:      /* LD IYH,A */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(IY, HIGH_REGISTER(AF));
                        break;

                    case 0x68:      /* LD IYL,B */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IY, HIGH_REGISTER(BC));
                        break;

                    case 0x69:      /* LD IYL,C */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IY, LOW_REGISTER(BC));
                        break;

                    case 0x6a:      /* LD IYL,D */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IY, HIGH_REGISTER(DE));
                        break;

                    case 0x6b:      /* LD IYL,E */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IY, LOW_REGISTER(DE));
                        break;

                    case 0x6c:      /* LD IYL,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IY, HIGH_REGISTER(IY));
                        break;

                    case 0x6d:      /* LD IYL,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE; /* nop */
                        break;

                    case 0x6e:      /* LD L,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_LOW_REGISTER(HL, GetBYTE(adr));
                        break;

                    case 0x6f:      /* LD IYL,A */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_LOW_REGISTER(IY, HIGH_REGISTER(AF));
                        break;

                    case 0x70:      /* LD (IY+dd),B */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, HIGH_REGISTER(BC));
                        break;

                    case 0x71:      /* LD (IY+dd),C */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, LOW_REGISTER(BC));
                        break;

                    case 0x72:      /* LD (IY+dd),D */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, HIGH_REGISTER(DE));
                        break;

                    case 0x73:      /* LD (IY+dd),E */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, LOW_REGISTER(DE));
                        break;

                    case 0x74:      /* LD (IY+dd),H */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, HIGH_REGISTER(HL));
                        break;

                    case 0x75:      /* LD (IY+dd),L */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, LOW_REGISTER(HL));
                        break;

                    case 0x77:      /* LD (IY+dd),A */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        PutBYTE(adr, HIGH_REGISTER(AF));
                        break;

                    case 0x7c:      /* LD A,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(AF, HIGH_REGISTER(IY));
                        break;

                    case 0x7d:      /* LD A,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        SET_HIGH_REGISTER(AF, LOW_REGISTER(IY));
                        break;

                    case 0x7e:      /* LD A,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        SET_HIGH_REGISTER(AF, GetBYTE(adr));
                        break;

                    case 0x84:      /* ADD A,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = HIGH_REGISTER(IY);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp;
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x85:      /* ADD A,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IY);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp;
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x86:      /* ADD A,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp;
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x8c:      /* ADC A,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = HIGH_REGISTER(IY);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp + TSTFLAG(C);
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x8d:      /* ADC A,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IY);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp + TSTFLAG(C);
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x8e:      /* ADC A,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        acu = HIGH_REGISTER(AF);
                        sum = acu + temp + TSTFLAG(C);
                        AF = addTable[sum] | cbitsZ80Table[acu ^ temp ^ sum];
                        break;

                    case 0x96:      /* SUB (IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp;
                        AF = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0x94:      /* SUB IYH */
                        SETFLAG(C, 0);/* fall through, a bit less efficient but smaller code */

                    case 0x9c:      /* SBC A,IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = HIGH_REGISTER(IY);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp - TSTFLAG(C);
                        AF = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0x95:      /* SUB IYL */
                        SETFLAG(C, 0);/* fall through, a bit less efficient but smaller code */

                    case 0x9d:      /* SBC A,IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IY);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp - TSTFLAG(C);
                        AF = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0x9e:      /* SBC A,(IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp - TSTFLAG(C);
                        AF = addTable[sum & 0xff] | cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0xa4:      /* AND IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = andTable[((AF & IY) >> 8) & 0xff];
                        break;

                    case 0xa5:      /* AND IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = andTable[((AF >> 8) & IY) & 0xff];
                        break;

                    case 0xa6:      /* AND (IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        AF = andTable[((AF >> 8) & GetBYTE(adr)) & 0xff];
                        break;

                    case 0xac:      /* XOR IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = xororTable[((AF ^ IY) >> 8) & 0xff];
                        break;

                    case 0xad:      /* XOR IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = xororTable[((AF >> 8) ^ IY) & 0xff];
                        break;

                    case 0xae:      /* XOR (IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        AF = xororTable[((AF >> 8) ^ GetBYTE(adr)) & 0xff];
                        break;

                    case 0xb4:      /* OR IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = xororTable[((AF | IY) >> 8) & 0xff];
                        break;

                    case 0xb5:      /* OR IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        AF = xororTable[((AF >> 8) | IY) & 0xff];
                        break;

                    case 0xb6:      /* OR (IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        AF = xororTable[((AF >> 8) | GetBYTE(adr)) & 0xff];
                        break;

                    case 0xbc:      /* CP IYH */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = HIGH_REGISTER(IY);
                        AF = (AF & ~0x28) | (temp & 0x28);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp;
                        AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                            cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0xbd:      /* CP IYL */
                        tStates += 9;
                        sim_brk_pend[0] = FALSE;
                        temp = LOW_REGISTER(IY);
                        AF = (AF & ~0x28) | (temp & 0x28);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp;
                        AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                            cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0xbe:      /* CP (IY+dd) */
                        tStates += 19;
                        adr = IY + (int8) RAM_PP(PC);
                        CHECK_BREAK_BYTE(adr);
                        temp = GetBYTE(adr);
                        AF = (AF & ~0x28) | (temp & 0x28);
                        acu = HIGH_REGISTER(AF);
                        sum = acu - temp;
                        AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                            cbits2Z80Table[(acu ^ temp ^ sum) & 0x1ff];
                        break;

                    case 0xcb:      /* CB prefix */
                        adr = IY + (int8) RAM_PP(PC);
                        switch ((op = GetBYTE(PC)) & 7) {

                            case 0:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = HIGH_REGISTER(BC);
                                break;

                            case 1:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = LOW_REGISTER(BC);
                                break;

                            case 2:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = HIGH_REGISTER(DE);
                                break;

                            case 3:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = LOW_REGISTER(DE);
                                break;

                            case 4:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = HIGH_REGISTER(HL);
                                break;

                            case 5:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = LOW_REGISTER(HL);
                                break;

                            case 6:
                                CHECK_BREAK_BYTE(adr);
                                ++PC;
                                acu = GetBYTE(adr);
                                break;

                            case 7:
                                sim_brk_pend[0] = FALSE;
                                ++PC;
                                acu = HIGH_REGISTER(AF);
                                break;
                        }
                        switch (op & 0xc0) {

                            case 0x00:  /* shift/rotate */
                                tStates += 23;
                                switch (op & 0x38) {

                                    case 0x00:  /* RLC */
                                        temp = (acu << 1) | (acu >> 7);
                                        cbits = temp & 1;
                                        goto cbshflg3;

                                    case 0x08:  /* RRC */
                                        temp = (acu >> 1) | (acu << 7);
                                        cbits = temp & 0x80;
                                        goto cbshflg3;

                                    case 0x10:  /* RL */
                                        temp = (acu << 1) | TSTFLAG(C);
                                        cbits = acu & 0x80;
                                        goto cbshflg3;

                                    case 0x18:  /* RR */
                                        temp = (acu >> 1) | (TSTFLAG(C) << 7);
                                        cbits = acu & 1;
                                        goto cbshflg3;

                                    case 0x20:  /* SLA */
                                        temp = acu << 1;
                                        cbits = acu & 0x80;
                                        goto cbshflg3;

                                    case 0x28:  /* SRA */
                                        temp = (acu >> 1) | (acu & 0x80);
                                        cbits = acu & 1;
                                        goto cbshflg3;

                                    case 0x30:  /* SLIA */
                                        temp = (acu << 1) | 1;
                                        cbits = acu & 0x80;
                                        goto cbshflg3;

                                    case 0x38:  /* SRL */
                                        temp = acu >> 1;
                                        cbits = acu & 1;
                                        cbshflg3:
                                        AF = (AF & ~0xff) | rotateShiftTable[temp & 0xff] | !!cbits;
                                        /* !!cbits == 0 if cbits == 0 !!cbits == 1 if cbits > 0 */
                                }
                                break;

                            case 0x40:  /* BIT */
                                tStates += 20;
                                if (acu & (1 << ((op >> 3) & 7)))
                                    AF = (AF & ~0xfe) | 0x10 | (((op & 0x38) == 0x38) << 7);
                                else
                                    AF = (AF & ~0xfe) | 0x54;
                                if ((op & 7) != 6)
                                    AF |= (acu & 0x28);
                                temp = acu;
                                break;

                            case 0x80:  /* RES */
                                tStates += 23;
                                temp = acu & ~(1 << ((op >> 3) & 7));
                                break;

                            case 0xc0:  /* SET */
                                tStates += 23;
                                temp = acu | (1 << ((op >> 3) & 7));
                                break;
                        }
                        switch (op & 7) {

                            case 0:
                                SET_HIGH_REGISTER(BC, temp);
                                break;

                            case 1:
                                SET_LOW_REGISTER(BC, temp);
                                break;

                            case 2:
                                SET_HIGH_REGISTER(DE, temp);
                                break;

                            case 3:
                                SET_LOW_REGISTER(DE, temp);
                                break;

                            case 4:
                                SET_HIGH_REGISTER(HL, temp);
                                break;

                            case 5:
                                SET_LOW_REGISTER(HL, temp);
                                break;

                            case 6:
                                PutBYTE(adr, temp);
                                break;

                            case 7:
                                SET_HIGH_REGISTER(AF, temp);
                                break;
                        }
                        break;

                    case 0xe1:      /* POP IY */
                        tStates += 14;
                        CHECK_BREAK_WORD(SP);
                        POP(IY);
                        break;

                    case 0xe3:      /* EX (SP),IY */
                        tStates += 23;
                        CHECK_BREAK_WORD(SP);
                        temp = IY;
                        POP(IY);
                        PUSH(temp);
                        break;

                    case 0xe5:      /* PUSH IY */
                        tStates += 15;
                        CHECK_BREAK_WORD(SP - 2);
                        PUSH(IY);
                        break;

                    case 0xe9:      /* JP (IY) */
                        tStates += 8;
                        sim_brk_pend[0] = FALSE;
                        PCQ_ENTRY(PCX);
                        PC = IY;
                        break;

                    case 0xf9:      /* LD SP,IY */
                        tStates += 10;
                        sim_brk_pend[0] = FALSE;
                        SP = IY;
                        break;

                    default:        /* ignore FD */
                        sim_brk_pend[0] = FALSE;
                        CHECK_CPU_Z80;
                        PC--;
                }
            break;

        case 0xfe:  /* CP nn */
            tStates += 7; /* CPI nn 7 */
            sim_brk_pend[0] = FALSE;
            temp = RAM_PP(PC);
            AF = (AF & ~0x28) | (temp & 0x28);
            acu = HIGH_REGISTER(AF);
            sum = acu - temp;
            cbits = acu ^ temp ^ sum;
            AF = (AF & ~0xff) | cpTable[sum & 0xff] | (temp & 0x28) |
                (SET_PV) | cbits2Table[cbits & 0x1ff];
            break;

        case 0xff:  /* RST 38H */
            tStates += 11; /* RST 7 11 */
            CHECK_BREAK_WORD(SP - 2);
            PUSH(PC);
            PCQ_ENTRY(PCX);
            PC = 0x38;
        }
    }

    /* It we stopped processing instructions because of a switch to the other
     * CPU, then fixup the reason code.
     */
    if (switch_cpu_now == FALSE) {
        reason = SCPE_OK;
    }

    end_decode:

    /* simulation halted */
    PC_S = ((reason == STOP_OPCODE) || (reason == STOP_MEM)) ? PCX : (PC & ADDRMASK);
    pcq_r -> qptr = pcq_p;  /* update pc q ptr */
    AF_S = AF;
    BC_S = BC;
    DE_S = DE;
    HL_S = HL;
    IX_S = IX;
    IY_S = IY;
    SP_S = SP;
    executedTStates = tStates;
    return reason;
}

/* reset routine */

static t_stat cpu_reset(DEVICE *dptr) {
    int32 i;
    AF_S = AF1_S = 0;
    BC_S = DE_S = HL_S = 0;
    BC1_S = DE1_S = HL1_S = 0;
    IR_S = IX_S = IY_S = SP_S = 0;
    IFF_S = 3;
    setBankSelect(0);
    cpu8086reset();
    sim_brk_types = (SWMASK('E') | SWMASK('I') | SWMASK('M'));
    sim_brk_dflt = SWMASK('E');
    for (i = 0; i < PCQ_SIZE; i++)
        pcq[i] = 0;
    pcq_p = 0;
    pcq_r = find_reg("PCQ", NULL, dptr);
    if (pcq_r)
        pcq_r -> qptr = 0;
    else
        return SCPE_IERR;
    return SCPE_OK;
}

t_stat install_bootrom(int32 bootrom[], int32 size, int32 addr, int32 makeROM) {
    int32 i;
    if (addr & (PAGESIZE - 1))
        return SCPE_IERR;
    for (i = 0; i < size; i++) {
        if (makeROM && ((i & (PAGESIZE - 1)) == 0))
            mmu_table[(i + addr) >> LOG2PAGESIZE] = ROM_PAGE;
        M[i + addr] = bootrom[i] & 0xff;
    }
    return SCPE_OK;
}

/* memory examine */
static t_stat cpu_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw) {
    int32 oldBankSelect;
    if (chiptype == CHIP_TYPE_8086)
        *vptr = GetBYTEExtended(addr);
    else {
        oldBankSelect = getBankSelect();
        setBankSelect((addr >> MAXBANKSIZELOG2) & BANKMASK);
        *vptr = GetBYTE(addr & ADDRMASK);
        setBankSelect(oldBankSelect);
    }
    return SCPE_OK;
}

/* memory deposit */
static t_stat cpu_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw) {
    int32 oldBankSelect;
    if (chiptype == CHIP_TYPE_8086)
        PutBYTEExtended(addr, val);
    else {
        oldBankSelect = getBankSelect();
        setBankSelect((addr >> MAXBANKSIZELOG2) & BANKMASK);
        PutBYTE(addr & ADDRMASK, val);
        setBankSelect(oldBankSelect);
    }
    return SCPE_OK;
}

struct cpuflag {
    int32 mask;     /* bit mask within CPU status register  */
    char name;      /* character to print if flag is set    */
};
typedef struct cpuflag CPUFLAG;

static CPUFLAG cpuflags8086[] = {
    {1 << 11, 'O'},
    {1 << 10, 'D'},
    {1 << 9, 'I'},
    {1 << 8, 'T'},
    {1 << 7, 'S'},
    {1 << 6, 'Z'},
    {1 << 4, 'A'},
    {1 << 2, 'P'},
    {1 << 0, 'C'},
    {0, 0}          /* last mask must be 0 */
};

static CPUFLAG cpuflags8080[] = {
    {1 << 7, 'S'},
    {1 << 6, 'Z'},
    {1 << 4, 'A'},
    {1 << 3, 'P'},
    {1 << 1, 'N'},
    {1 << 0, 'C'},
    {0, 0}          /* last mask must be 0 */
};

static CPUFLAG cpuflagsZ80[] = {
    {1 << 7, 'S'},
    {1 << 6, 'Z'},
    {1 << 4, 'A'},
    {1 << 3, 'V'},
    {1 << 1, 'N'},
    {1 << 0, 'C'},
    {0, 0}          /* last mask must be 0 */
};

/* needs to be set for each chiptype <= MAX_CHIP_TYPE */
static char *chipTypeToString[] =   { "8080",       "Z80",          "8086"          };
static int32 *flagregister[] =      { &AF_S,        &AF_S,          &FLAGS_S        };
static CPUFLAG *cpuflags[] =        { cpuflags8080, cpuflagsZ80,    cpuflags8086    };

/* needs to be set for each ramtype <= MAX_RAM_TYPE */
static char *ramTypeToString[] = { "AZ80", "HRAM", "VRAM", "CRAM" };

static t_stat chip_show(FILE *st, UNIT *uptr, int32 val, void *desc) {
    fprintf(st, cpu_unit.flags & UNIT_CPU_OPSTOP ? "ITRAP, " : "NOITRAP, ");
    if (chiptype <= MAX_CHIP_TYPE)
        fprintf(st, "%s", chipTypeToString[chiptype]);
    fprintf(st, ", ");
    if (ramtype <= MAX_RAM_TYPE)
        fprintf(st, "%s", ramTypeToString[ramtype]);
    return SCPE_OK;
}

static t_stat cpu_show(FILE *st, UNIT *uptr, int32 val, void *desc) {
    uint32 i, maxBanks, first = TRUE;
    MDEV m;
    maxBanks = ((cpu_unit.flags & UNIT_CPU_BANKED) ||
        (chiptype == CHIP_TYPE_8086)) ? MAXBANKS : 1;
    fprintf(st, "VERBOSE,\n       ");
    for (i = 0; i < 4; i++)
        fprintf(st, "0123456789ABCDEF");
    fprintf(st, " [16k]");
    for (i = 0; i < (maxBanks * (MAXBANKSIZE >> LOG2PAGESIZE)); i++) {
        if ((i & 0x3f) == 0)
            fprintf(st, "\n%05X: ", (i << LOG2PAGESIZE));
        m = mmu_table[i];
        if (m.isRAM)
            fprintf(st, "W");
        else if (m.isEmpty)
            fprintf(st, "U");
        else if (m.routine)
            fprintf(st, "M");
        else
            fprintf(st, "R");
    }
    fprintf(st, ",\n0x[");
    /* show which ports are assigned */
    for (i = 0; i < 256; i++)
        if (dev_table[i].routine != &nulldev) {
            if (first)
                first = FALSE;
            else
                fprintf(st, " ");
            fprintf(st, "%02X", i);
        }
    fprintf(st, "]");
    if (chiptype <= MAX_CHIP_TYPE) {
        first = TRUE;
        /* show verbose CPU flags */
        for (i = 0; cpuflags[chiptype][i].mask; i++)
            if (*flagregister[chiptype] & cpuflags[chiptype][i].mask) {
                if (first) {
                    first = FALSE;
                    fprintf(st, " ");
                }
                fprintf(st, "%c", cpuflags[chiptype][i].name);
            }
    }
    return SCPE_OK;
}

static void cpu_clear(void) {
    uint32 i;
    for (i = 0; i < MAXMEMORY; i++)
        M[i] = 0;
    for (i = 0; i < (MAXMEMORY >> LOG2PAGESIZE); i++)
        mmu_table[i] = RAM_PAGE;
    for (i = (MEMORYSIZE >> LOG2PAGESIZE); i < (MAXMEMORY >> LOG2PAGESIZE); i++)
        mmu_table[i] = EMPTY_PAGE;
    if (cpu_unit.flags & UNIT_CPU_ALTAIRROM)
        install_ALTAIRbootROM();
    clockHasChanged = FALSE;
}

static t_stat cpu_clear_command(UNIT *uptr, int32 value, char *cptr, void *desc) {
    cpu_clear();
    return SCPE_OK;
}

static t_stat cpu_set_altairrom(UNIT *uptr, int32 value, char *cptr, void *desc) {
    install_ALTAIRbootROM();
    return SCPE_OK;
}

static t_stat cpu_set_noaltairrom(UNIT *uptr, int32 value, char *cptr, void *desc) {
    mmu_table[ALTAIR_ROM_LOW >> LOG2PAGESIZE] = MEMORYSIZE < MAXBANKSIZE ?
        EMPTY_PAGE : RAM_PAGE;
    return SCPE_OK;
}

static t_stat cpu_set_nommu(UNIT *uptr, int32 value, char *cptr, void *desc) {
    if (chiptype == CHIP_TYPE_8086) {
        printf("Cannot switch off MMU for 8086 CPU.\n");
        return SCPE_ARG;
    }
    if (cpu_unit.flags & UNIT_CPU_BANKED) {
        printf("Cannot switch off MMU for banked memory.\n");
        return SCPE_ARG;
    }
    if (((chiptype == CHIP_TYPE_8080) || (chiptype == CHIP_TYPE_Z80)) &&
            (MEMORYSIZE < MAXBANKSIZE)) {
        printf("Cannot switch off MMU when memory is %iKB < %iKB.\n",
            MEMORYSIZE >> KBLOG2, MAXBANKSIZE >> KBLOG2);
        return SCPE_ARG;
    }
    return SCPE_OK;
}

static t_stat cpu_set_banked(UNIT *uptr, int32 value, char *cptr, void *desc) {
    if ((chiptype == CHIP_TYPE_8080) || (chiptype == CHIP_TYPE_Z80)) {
        if (MEMORYSIZE <= MAXBANKSIZE)
            previousCapacity = MEMORYSIZE;
        MEMORYSIZE = MAXMEMORY;
        cpu_dev.awidth = MAXBANKSIZELOG2 + MAXBANKSLOG2;
        cpu_clear();
    }
    else if (chiptype == CHIP_TYPE_8086) {
        printf("Cannot use banked memory for 8086 CPU.\n");
        return SCPE_ARG;
    }
    return SCPE_OK;
}

static t_stat cpu_set_nonbanked(UNIT *uptr, int32 value, char *cptr, void *desc) {
    if ((chiptype == CHIP_TYPE_8080) || (chiptype == CHIP_TYPE_Z80)) {
        MEMORYSIZE = previousCapacity;
        cpu_dev.awidth = MAXBANKSIZELOG2;
        cpu_clear();
    }
    return SCPE_OK;
}

static int32 bankseldev(const int32 port, const int32 io, const int32 data) {
    if (io) {
        switch(ramtype) {
            case 1:
                if (data & 0x40) {
                    printf("HRAM: Parity %s" NLP, data & 1 ? "ON" : "OFF");
                } else {
                    printf("HRAM BANKSEL=%02x" NLP, data);
                }
                break;
            case 2:
/*              printf("VRAM BANKSEL=%02x" NLP, data);*/
                switch(data & 0xFF) {
                    case 0x01:
/*                  case 0x41:      // OASIS uses this for some reason? */
                        setBankSelect(0);
                        break;
                    case 0x02:
/*                  case 0x42:      // OASIS uses this for some reason? */
                        setBankSelect(1);
                        break;
                    case 0x04:
                        setBankSelect(2);
                        break;
                    case 0x08:
                        setBankSelect(3);
                        break;
                    case 0x10:
                        setBankSelect(4);
                        break;
                    case 0x20:
                        setBankSelect(5);
                        break;
                    case 0x40:
                        setBankSelect(6);
                        break;
                    case 0x80:
                        setBankSelect(7);
                        break;
                    default:
/*                      printf("Invalid bank select 0x%02x for VRAM" NLP, data);*/
                        break;
                }
                break;
            case 3:
/*                printf(ADDRESS_FORMAT " CRAM BANKSEL=%02x" NLP, PCX, data); */
                switch(data & 0x7F) {
                    case 0x01:
                        setBankSelect(0);
                        break;
                    case 0x02:
                        setBankSelect(1);
                        break;
                    case 0x04:
                        setBankSelect(2);
                        break;
                    case 0x08:
                        setBankSelect(3);
                        break;
                    case 0x10:
                        setBankSelect(4);
                        break;
                    case 0x20:
                        setBankSelect(5);
                        break;
                    case 0x40:
                        setBankSelect(6);
                        break;
/*                    case 0x80: */
/*                        setBankSelect(7); */
/*                        break; */
                    default:
                        printf("Invalid bank select 0x%02x for CRAM" NLP, data);
                        break;
                }

                break;
            case 0:
            default:
                break;
        }
        return 0;
    } else {
        return(0xFF);
    }
}

static void cpu_set_chiptype_short(int32 value, uint32 need_cpu_clear) {
    extern REG *sim_PC;
    if ((chiptype == value) || (chiptype > MAX_CHIP_TYPE))
        return; /* nothing to do */
    if (((chiptype == CHIP_TYPE_8080) && (value == CHIP_TYPE_Z80)) ||
        ((chiptype == CHIP_TYPE_Z80) && (value == CHIP_TYPE_8080))) {
        chiptype = value;
        return;
    }
    chiptype = value;
    if (chiptype == CHIP_TYPE_8086) {
        if (MEMORYSIZE <= MAXBANKSIZE)
            previousCapacity = MEMORYSIZE;
        MEMORYSIZE = MAXMEMORY;
        cpu_unit.flags &= ~(UNIT_CPU_BANKED | UNIT_CPU_ALTAIRROM);
        cpu_unit.flags |= UNIT_CPU_MMU;
        cpu_dev.awidth = MAXBANKSIZELOG2 + MAXBANKSLOG2;
        if (need_cpu_clear)
            cpu_clear();
        sim_PC = &cpu_reg[7];
    }
    else if ((chiptype == CHIP_TYPE_8080) || (chiptype == CHIP_TYPE_Z80)) {
        MEMORYSIZE = previousCapacity;
        cpu_dev.awidth = MAXBANKSIZELOG2;
        if (need_cpu_clear)
            cpu_clear();
        sim_PC = &cpu_reg[6];
    }
}

static t_stat cpu_set_chiptype(UNIT *uptr, int32 value, char *cptr, void *desc) {
    cpu_set_chiptype_short(value, TRUE);
    return SCPE_OK;
}

static int32 switchcpu_io(const int32 port, const int32 io, const int32 data) {
    int32 new_chiptype = 0;
    if (io == 0) { /* Read, switch CPU */
        switch(chiptype) {
            case CHIP_TYPE_8080:
            case CHIP_TYPE_Z80:
                if (cpu_unit.flags & UNIT_CPU_VERBOSE) {
                    printf("CPU: " ADDRESS_FORMAT " SWITCH(port=%02x) to 8086" NLP, PCX, port);
                }
                new_chiptype = CHIP_TYPE_8086;
                switch_cpu_now = FALSE; /* hharte */
                break;
            case CHIP_TYPE_8086:
                if (cpu_unit.flags & UNIT_CPU_VERBOSE) {
                    printf("CPU: " ADDRESS_FORMAT " SWITCH(port=%02x) to 8085/Z80" NLP, PCX, port);
                }
                new_chiptype = CHIP_TYPE_Z80;
                switch_cpu_now = FALSE; /* hharte */
                break;
            default:
                printf("%s: invalid chiptype: %d\n", __FUNCTION__, chiptype);
                break;
        }

        cpu_set_chiptype_short(new_chiptype, FALSE);
        return(0xFF); /* Return High-Z Data */
    } else {
        printf("%s: Set EXT_ADDR=%02x\n", __FUNCTION__, data);
    }
    return 0;
}

static t_stat cpu_show_switcher(FILE *st, UNIT *uptr, int32 val, void *desc) {
    if ((cpu_unit.flags & UNIT_CPU_SWITCHER) && (switcherPort >= 0))
        fprintf(st, "SWITCHER=0x%02x", switcherPort);
    else
        fprintf(st, "NOSWITCHER");
    return SCPE_OK;
}

static t_stat cpu_set_switcher(UNIT *uptr, int32 value, char *cptr, void *desc) {
    struct idev safe;
    switcherPort &= 0xff;
    safe = dev_table[switcherPort];
    if (sim_map_resource(switcherPort, 1, RESOURCE_TYPE_IO, &switchcpu_io, FALSE)) {
        printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, switcherPort);
        return SCPE_ARG;
    }
    oldSwitcherDevice = safe;
    return SCPE_OK;
}

static t_stat cpu_reset_switcher(UNIT *uptr, int32 value, char *cptr, void *desc) {
    if (sim_map_resource(switcherPort, 1, RESOURCE_TYPE_IO, oldSwitcherDevice.routine, FALSE)) {
        printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, switcherPort);
        return SCPE_ARG;
    }
    return SCPE_OK;
}

static t_stat cpu_set_ramtype(UNIT *uptr, int32 value, char *cptr, void *desc) {

    if (value == ramtype) {
        if (cpu_unit.flags & UNIT_CPU_VERBOSE)
            printf("RAM Selection unchanged\n");
        return SCPE_OK;
    }

    switch(ramtype) {
        case 1:
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                printf("Unmapping NorthStar HRAM\n");
            sim_map_resource(0xC0, 1, RESOURCE_TYPE_IO, &bankseldev, TRUE);
            break;
        case 2:
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                printf("Unmapping Vector RAM\n");
            sim_map_resource(0x40, 1, RESOURCE_TYPE_IO, &bankseldev, TRUE);
            break;
        case 3:
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                printf("Unmapping Cromemco RAM\n");
            sim_map_resource(0x40, 1, RESOURCE_TYPE_IO, &bankseldev, TRUE);
            break;
        case 0:
        default:
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                printf("Unmapping AltairZ80 RAM\n");
            break;
    }

    switch(value) {
        case 1:
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                printf("NorthStar HRAM Selected\n");
            sim_map_resource(0xC0, 1, RESOURCE_TYPE_IO, &bankseldev, FALSE);
            break;
        case 2:
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                printf("Vector RAM Selected\n");
            sim_map_resource(0x40, 1, RESOURCE_TYPE_IO, &bankseldev, FALSE);
            break;
        case 3:
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                printf("Cromemco RAM Selected\n");
            sim_map_resource(0x40, 1, RESOURCE_TYPE_IO, &bankseldev, FALSE);
            break;
        case 0:
        default:
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                printf("AltairZ80 RAM Selected\n");
            break;
    }

    ramtype = value;
    return SCPE_OK;
}

/* set memory to 'size' kilo byte */
static t_stat set_size(uint32 size) {
    uint32 maxsize = (((chiptype == CHIP_TYPE_8080) || (chiptype == CHIP_TYPE_Z80)) &&
        ((cpu_unit.flags & UNIT_CPU_BANKED) == 0)) ? MAXBANKSIZE : MAXMEMORY;
    size <<= KBLOG2;
    if (cpu_unit.flags & UNIT_CPU_BANKED)
        size &= ~ADDRMASK;
    cpu_unit.flags |= UNIT_CPU_MMU;
    if (size < KB)
        MEMORYSIZE = KB;
    else if (size > maxsize)
        MEMORYSIZE = maxsize;
    else
        MEMORYSIZE = size;
    cpu_dev.awidth = MAXBANKSIZELOG2;
    if  (size > MAXBANKSIZE)
        cpu_dev.awidth += MAXBANKSLOG2;
    cpu_clear();
    return SCPE_OK;
}

static t_stat cpu_set_size(UNIT *uptr, int32 value, char *cptr, void *desc) {
    return set_size(value);
}

static t_stat cpu_set_memory(UNIT *uptr, int32 value, char *cptr, void *desc) {
    uint32 size, result, i;
    if (cptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%i%n", &size, &i);
    if ((result == 1) && (cptr[i] == 'K') && ((cptr[i + 1] == 0) ||
            ((cptr[i + 1] == 'B') && (cptr[i + 2] == 0))))
        return set_size(size);
    return SCPE_ARG;
}

t_value altairz80_pc_value (void) {
    return (t_value)PCX;
}

/* AltairZ80 Simulator initialization */
void altairz80_init(void) {
    cpu_clear();
    sim_vm_pc_value = &altairz80_pc_value;
/* altairz80_print_tables(); */
}

void (*sim_vm_init) (void) = &altairz80_init;

/*  This is the binary loader. The input file is considered to be a string of
    literal bytes with no special format. The load starts at the current value
    of the PC if no start address is given. If the input string ends with ROM
    (not case sensitive) the memory area is made read only.
    ALTAIRROM/NOALTAIRROM settings are ignored.
*/

#define PLURAL(x) (x), (x) == 1 ? "" : "s"

t_stat sim_load(FILE *fileref, char *cptr, char *fnam, int flag) {
    int32 i;
    uint32 addr, cnt = 0, org, pagesModified = 0, makeROM = FALSE;
    t_addr j, lo, hi;
    char *result;
    MDEV m;
    char gbuf[CBUFSIZE];
    if (flag) {
        result = get_range(NULL, cptr, &lo, &hi, 16, ADDRMASKEXTENDED, 0);
        if (result == NULL)
            return SCPE_ARG;
        for (j = lo; j <= hi; j++) {
            if (putc(GetBYTEExtended(j), fileref) == EOF)
                return SCPE_IOERR;
        }
        printf("%d byte%s dumped [%x - %x].\n", PLURAL(hi + 1 - lo), lo, hi);
    }
    else {
        if (*cptr == 0)
            addr = (chiptype == CHIP_TYPE_8086) ? PCX_S : PC_S;
        else {
            get_glyph(cptr, gbuf, 0);
            if (strcmp(gbuf, "ROM") == 0) {
                addr = (chiptype == CHIP_TYPE_8086) ? PCX_S : PC_S;
                makeROM = TRUE;
            }
            else {
                addr = strtotv(cptr, &result, 16) & ADDRMASKEXTENDED;
                if (cptr == result)
                    return SCPE_ARG;
                while (isspace(*result)) result++;
                get_glyph(result, gbuf, 0);
                if (strcmp(gbuf, "ROM") == 0)
                    makeROM = TRUE;
            }
        }
        /* addr is start address to load to, makeROM == TRUE iff memory should become ROM */
        org = addr;
        while ((addr < MAXMEMORY) && ((i = getc(fileref)) != EOF)) {
            m = mmu_table[addr >> LOG2PAGESIZE];
            if (!m.isRAM && m.isEmpty) {
                mmu_table[addr >> LOG2PAGESIZE] = RAM_PAGE;
                pagesModified++;
                m = RAM_PAGE;
            }
            if (makeROM) {
                mmu_table[addr >> LOG2PAGESIZE] = ROM_PAGE;
                m = ROM_PAGE;
            }
            if (!m.isRAM && m.routine)
                m.routine(addr, 1, i);
            else
                M[addr] = i;
            addr++;
            cnt++;
        } /* end while */
        printf("%d byte%s [%d page%s] loaded at %x%s.\n", PLURAL(cnt),
            PLURAL((cnt + 0xff) >> 8), org, makeROM ? " [ROM]" : "");
        if (pagesModified)
            printf("Warning: %d page%s modified.\n", PLURAL(pagesModified));
    }
    return SCPE_OK;
}

void cpu_raise_interrupt(uint32 irq) {
    extern void cpu8086_intr(uint8 intrnum);

    if (chiptype == CHIP_TYPE_8086) {
        cpu8086_intr(irq);
    } else if (cpu_unit.flags & UNIT_CPU_VERBOSE) {
        printf("Interrupts not fully supported for chiptype: %s\n",
               (chiptype <= MAX_CHIP_TYPE) ? chipTypeToString[chiptype] : "????");
    }
}
