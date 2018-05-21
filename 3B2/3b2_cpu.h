/* 3b2_cpu.h: AT&T 3B2 Model 400 CPU (WE32100) Header

   Copyright (c) 2017, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

#ifndef _3B2_CPU_H_
#define _3B2_CPU_H_

#include "3b2_defs.h"
#include "3b2_io.h"

/* Execution Modes */
#define EX_LVL_KERN           0
#define EX_LVL_EXEC           1
#define EX_LVL_SUPR           2
#define EX_LVL_USER           3

/* Processor Version Number */
#define WE32100_VER           0x1A

/*
 *
 * Mode                   Syntax     Mode          Reg.    Bytes    Notes
 * ----------------------------------------------------------------------
 * Absolute               $expr         7           15     5
 * Abs. Deferred          *$expr       14           15     5
 * Byte Disp.             expr(%rn)    12   0-10,12-15     2
 * Byte Disp. Def.        *expr(%rn)   13   0-10,12-15     2
 * Halfword Disp.         expr(%rn)    10   0-10,12-15     3
 * Halfword Disp. Def.    *expr(%rn)   11   0-10,12-15     3
 * Word Disp.             expr(%rn)     8   0-10,12-15     5
 * Word Disp. Def.        *expr(%rn)    9   0-10,12-15     5
 * AP Short Offset        so(%ap)       7         0-14     1         1
 * FP Short Offset        so(%fp)       6         0-14     1         1
 * Byte Immediate         &imm8         6           15     2         2,3
 * Halfword Immediate     &imm16        5           15     3         2,3
 * Word Immediate         &imm32        4           15     5         2,3
 * Positive Literal       &lit        0-3         0-15     1         2,3
 * Negative Literal       &lit         15         0-15     1         2,3
 * Register               %rn           4         0-14     1         1,3
 * Register Deferred      (%rn)         5   0-10,12-14     1         1
 * Expanded Op. Type      {type}opnd   14         0-14   2-6         4
 *
 * Notes:
 *
 * 1. Mode field has special meaning if register field is 15; see
 *    absolute or immediate mode.
 * 2. Mode may not be used for a destination operand.
 * 3. Mode may not be used if the instruction takes effective address
 *    of the operand.
 * 4. 'type' overrides instruction type; 'type' determines the operand
 *    type, except that it does not determine the length for immediate
 *    or literals or whether literals are signed or unsigned. 'opnd'
 *    determines actual address mode. For total bytes, add 1 to byte
 *    count for address mode determined by 'opnd'.
 *
 */

/*
 * Opcodes
 */
typedef enum {
    HALT    = 0x00, /* Undocumented instruction */
    SPOPRD  = 0x02,
    SPOPD2  = 0x03,
    MOVAW   = 0x04,
    SPOPRT  = 0x06,
    SPOPT2  = 0x07,
    RET     = 0x08,
    MOVTRW  = 0x0C,
    SAVE    = 0x10,
    SPOPWD  = 0x13,
    EXTOP   = 0x14,
    SPOPWT  = 0x17,
    RESTORE = 0x18,
    SWAPWI  = 0x1C,
    SWAPHI  = 0x1E,
    SWAPBI  = 0x1F,
    POPW    = 0x20,
    SPOPRS  = 0x22,
    SPOPS2  = 0x23,
    JMP     = 0x24,
    CFLUSH  = 0x27,
    TSTW    = 0x28,
    TSTH    = 0x2A,
    TSTB    = 0x2B,
    CALL    = 0x2C,
    BPT     = 0x2E,
    WAIT    = 0x2F,
    EMB     = 0x30, // Multi-byte
    SPOP    = 0x32,
    SPOPWS  = 0x33,
    JSB     = 0x34,
    BSBH    = 0x36,
    BSBB    = 0x37,
    BITW    = 0x38,
    BITH    = 0x3A,
    BITB    = 0x3B,
    CMPW    = 0x3C,
    CMPH    = 0x3E,
    CMPB    = 0x3F,
    RGEQ    = 0x40,
    BGEH    = 0x42,
    BGEB    = 0x43,
    RGTR    = 0x44,
    BGH     = 0x46,
    BGB     = 0x47,
    RLSS    = 0x48,
    BLH     = 0x4A,
    BLB     = 0x4B,
    RLEQ    = 0x4C,
    BLEH    = 0x4E,
    BLEB    = 0x4F,
    RGEQU   = 0x50,
    BGEUH   = 0x52,  // Also BCCH
    BGEUB   = 0x53,  // Also BCCB
    RGTRU   = 0x54,
    BGUH    = 0x56,
    BGUB    = 0x57,
    BLSSU   = 0x58,  // Also BCS
    BLUH    = 0x5A,  // Also BCSH
    BLUB    = 0x5B,  // Also BCSB
    RLEQU   = 0x5C,
    BLEUH   = 0x5E,
    BLEUB   = 0x5F,
    RVC     = 0x60,
    BVCH    = 0x62,
    BVCB    = 0x63,
    RNEQU   = 0x64,
    BNEH_D  = 0x66, // Duplicate of 0x76
    BNEB_D  = 0x67, // Duplicate of 0x77
    RVS     = 0x68,
    BVSH    = 0x6A,
    BVSB    = 0x6B,
    REQLU   = 0x6C,
    BEH_D   = 0x6E, // Duplicate of 0x7E
    BEB_D   = 0x6F, // Duplicate of 0x7F
    NOP     = 0x70,
    NOP3    = 0x72,
    NOP2    = 0x73,
    BNEQ    = 0x74,
    RNEQ    = 0x74,
    BNEH    = 0x76,
    BNEB    = 0x77,
    RSB     = 0x78,
    BRH     = 0x7A,
    BRB     = 0x7B,
    REQL    = 0x7C,
    BEH     = 0x7E,
    BEB     = 0x7F,
    CLRW    = 0x80,
    CLRH    = 0x82,
    CLRB    = 0x83,
    MOVW    = 0x84, /* done */
    MOVH    = 0x86, /* done */
    MOVB    = 0x87, /* done */
    MCOMW   = 0x88,
    MCOMH   = 0x8A,
    MCOMB   = 0x8B,
    MNEGW   = 0x8C,
    MNEGH   = 0x8E,
    MNEGB   = 0x8F,
    INCW    = 0x90,
    INCH    = 0x92,
    INCB    = 0x93,
    DECW    = 0x94,
    DECH    = 0x96,
    DECB    = 0x97,
    ADDW2   = 0x9C,
    ADDH2   = 0x9E,
    ADDB2   = 0x9F,
    PUSHW   = 0xA0,
    MODW2   = 0xA4,
    MODH2   = 0xA6,
    MODB2   = 0xA7,
    MULW2   = 0xA8,
    MULH2   = 0xAA,
    MULB2   = 0xAB,
    DIVW2   = 0xAC,
    DIVH2   = 0xAE,
    DIVB2   = 0xAF,
    ORW2    = 0xB0,
    ORH2    = 0xB2,
    ORB2    = 0xB3,
    XORW2   = 0xB4,
    XORH2   = 0xB6,
    XORB2   = 0xB7,
    ANDW2   = 0xB8,
    ANDH2   = 0xBA,
    ANDB2   = 0xBB,
    SUBW2   = 0xBC,
    SUBH2   = 0xBE,
    SUBB2   = 0xBF,
    ALSW3   = 0xC0,
    ARSW3   = 0xC4,
    ARSH3   = 0xC6,
    ARSB3   = 0xC7,
    INSFW   = 0xC8,
    INSFH   = 0xCA,
    INSFB   = 0xCB,
    EXTFW   = 0xCC,
    EXTFH   = 0xCE,
    EXTFB   = 0xCF,
    LLSW3   = 0xD0,
    LLSH3   = 0xD2,
    LLSB3   = 0xD3,
    LRSW3   = 0xD4,
    ROTW    = 0xD8,
    ADDW3   = 0xDC,
    ADDH3   = 0xDE,
    ADDB3   = 0xDF,
    PUSHAW  = 0xE0,
    MODW3   = 0xE4,
    MODH3   = 0xE6,
    MODB3   = 0xE7,
    MULW3   = 0xE8,
    MULH3   = 0xEA,
    MULB3   = 0xEB,
    DIVW3   = 0xEC,
    DIVH3   = 0xEE,
    DIVB3   = 0xEF,
    ORW3    = 0xF0,
    ORH3    = 0xF2,
    ORB3    = 0xF3,
    XORW3   = 0xF4,
    XORH3   = 0xF6,
    XORB3   = 0xF7,
    ANDW3   = 0xF8,
    ANDH3   = 0xFA,
    ANDB3   = 0xFB,
    SUBW3   = 0xFC,
    SUBH3   = 0xFE,
    SUBB3   = 0xFF,
    MVERNO  = 0x3009,
    ENBVJMP = 0x300d,
    DISVJMP = 0x3013,
    MOVBLW  = 0x3019,
    STREND  = 0x301f,
    INTACK  = 0x302f,
    STRCPY  = 0x3035,
    RETG    = 0x3045,
    GATE    = 0x3061,
    CALLPS  = 0x30ac,
    RETPS   = 0x30c8
} opcode;

typedef enum {
    DECODE_SUCCESS,
    DECODE_ERROR
} decode_result;

/* Addressing Mode enum */
typedef enum {
    ABS,              // Absolute
    ABS_DEF,          // Absolute Deferred
    BYTE_DISP,        // Byte Displacement
    BYTE_DISP_DEF,    // Byte Displacement Deferred
    HFWD_DISP,        // Halfword Displacement
    HFWD_DISP_DEF,    // Halfword Displacement Deferred
    WORD_DISP,        // Word Displacement
    WORD_DISP_DEF,    // Word Displacement Deferred
    AP_SHORT_OFF,     // AP Short Offset
    FP_SHORT_OFF,     // FP Short Offset
    BYTE_IMM,         // Byte Immediate
    HFWD_IMM,         // Halfword Immediate
    WORD_IMM,         // Word Immediate
    POS_LIT,          // Positive Literal
    NEG_LIT,          // Negative Literal
    REGISTER,         // Register
    REGISTER_DEF,     // Register Deferred
    EXP               // Expanded-operand type
} addr_mode;

/*
 * Each instruction expects operands of a certain type.
 *
 * The large majority of instructions expect operands that have a
 * descriptor as the first byte. This descriptor carries all the
 * information necessary to compute the addressing mode of the
 * operand.
 *
 * e.g.:
 *
 *   MOVB 6(%r1),%r0
 *   +------+------+------+------+
 *   | 0x87 | 0xc1 | 0x06 | 0x40 |
 *   +------+------+------+------+
 *           ^^^^^^
 *       Descriptor byte. mode = 13 (0x0c), register = 1 (0x01)
 *
 *
 * Branch instructions have either an 8-bit or a 16-bit signed
 * displacement value, and lack a descriptor byte.
 *
 * e.g.:
 *
 *   BCCB 0x03
 *   +------+------+
 *   | 0x53 | 0x03 |            8-bit displacement
 *   +------+------+
 *
 *   BCCH 0x01ff
 *   +------+------+------+
 *   | 0x52 | 0xff | 0x01 |    16-bit displacement
 *   +------+------+------+
 *
 *
 * TODO: Describe coprocessor instructions
 *
 */
typedef enum {
    OP_NONE, /* NULL type */
    OP_DESC, /* Descriptor byte */
    OP_BYTE, /* 8-bit signed value */
    OP_HALF, /* 16-bit signed value */
    OP_COPR  /* Coprocessor instruction */
} op_mode;

/* Describes a mnemonic */
typedef struct _mnemonic {
    uint16  opcode;
    int8    op_count;    /* Number of operands */
    op_mode mode;        /* Dispatch mode      */
    int8    dtype;       /* Default data type  */
    char    mnemonic[8];
    int8    src_op1;
    int8    src_op2;
    int8    src_op3;
    int8    dst_op;
} mnemonic;

/*
 * Structure that describes each operand in a decoded instruction
 */
typedef struct _operand {
    uint8   mode;        /* Embedded data addressing mode */
    uint8   reg;         /* Operand register (0-15) */
    int8    dtype;       /* Default type for the operand */
    int8    etype;       /* Expanded type (-1 if none) */
    union {
        uint32 w;
        uint16 h;
        uint8  b;
    } embedded;          /* Data consumed as part of the instruction
                            stream, i.e. literals, displacement,
                            etc. */
    uint32  data;        /* Data either read or written during
                            instruction execution */
} operand;

/*
 * An inst is a combination of a decoded instruction and
 * 0 to 4 operands. Also used for history record keeping.
 */
typedef struct _instr {
    mnemonic *mn;
    uint32 psw;
    uint32 sp;
    uint32 pc;
    t_bool valid;
    operand operands[4];
} instr;

/* Function prototypes */
t_stat sys_boot(int32 flag, CONST char *ptr);
t_stat cpu_svc(UNIT *uptr);
t_stat cpu_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset(DEVICE *dptr);
t_stat cpu_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_show_virt(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_show_stack(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_show_cio(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_halt(UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_clear_halt(UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_boot(int32 unit_num, DEVICE *dptr);

t_bool cpu_is_pc_a_subroutine_call (t_addr **ret_addrs);

void cpu_register_name(uint8 reg, char *buf, size_t len);
void cpu_show_operand(FILE *st, operand *op);
void fprint_sym_hist(FILE *st, instr *ip);
t_stat fprint_sym_m(FILE *of, t_addr addr, t_value *val);

instr *cpu_next_instruction(void);

uint8 decode_instruction(instr *instr);
void cpu_on_interrupt(uint16 vec);

static uint32 cpu_effective_address(operand * op);
static uint32 cpu_read_op(operand * op);
static void cpu_write_op(operand * op, t_uint64 val);
static void cpu_set_nz_flags(t_uint64 data, operand * op);
static void cpu_calc_ints();

static SIM_INLINE void cpu_on_normal_exception(uint8 isc);
static SIM_INLINE void cpu_on_stack_exception(uint8 isc);
static SIM_INLINE void cpu_on_process_exception(uint8 isc);
static SIM_INLINE void cpu_on_reset_exception(uint8 isc);
static SIM_INLINE void cpu_perform_gate(uint32 index1, uint32 index2);
static SIM_INLINE t_bool mem_exception();
static SIM_INLINE void clear_exceptions();
static SIM_INLINE void set_psw_tm(t_bool val);
static SIM_INLINE void clear_instruction(instr *inst);
static SIM_INLINE int8 op_type(operand *op);
static SIM_INLINE t_bool op_signed(operand *op);
static SIM_INLINE t_bool is_byte_immediate(operand * oper);
static SIM_INLINE t_bool is_halfword_immediate(operand * oper);
static SIM_INLINE t_bool is_word_immediate(operand * oper);
static SIM_INLINE t_bool is_positive_literal(operand * oper);
static SIM_INLINE t_bool is_negative_literal(operand * oper);
static SIM_INLINE t_bool invalid_destination(operand * oper);
static SIM_INLINE uint32 sign_extend_n(uint8 val);
static SIM_INLINE uint32 sign_extend_b(uint8 val);
static SIM_INLINE uint32 zero_extend_b(uint8 val);
static SIM_INLINE uint32 sign_extend_h(uint16 val);
static SIM_INLINE uint32 zero_extend_h(uint16 val);
static SIM_INLINE t_bool cpu_z_flag();
static SIM_INLINE t_bool cpu_n_flag();
static SIM_INLINE t_bool cpu_c_flag();
static SIM_INLINE t_bool cpu_v_flag();
static SIM_INLINE void cpu_set_z_flag(t_bool val);
static SIM_INLINE void cpu_set_n_flag(t_bool val);
static SIM_INLINE void cpu_set_c_flag(t_bool val);
static SIM_INLINE void cpu_set_v_flag(t_bool val);
static SIM_INLINE void cpu_set_v_flag_op(t_uint64 val, operand *op);
static SIM_INLINE uint8 cpu_execution_level();
static SIM_INLINE void cpu_set_execution_level(uint8 level);
static SIM_INLINE void cpu_push_word(uint32 val);
static SIM_INLINE uint32 cpu_pop_word();
static SIM_INLINE void irq_push_word(uint32 val);
static SIM_INLINE uint32 irq_pop_word();
static SIM_INLINE void cpu_context_switch_1(uint32 pcbp);
static SIM_INLINE void cpu_context_switch_2(uint32 pcbp);
static SIM_INLINE void cpu_context_switch_3(uint32 pcbp);
static SIM_INLINE t_bool op_is_psw(operand *op);
static SIM_INLINE t_bool op_is_sp(operand *op);
static SIM_INLINE uint32 cpu_next_pc();
static SIM_INLINE void add(t_uint64 a, t_uint64 b, operand *dst);
static SIM_INLINE void sub(t_uint64 a, t_uint64 b, operand *dst);

/* Helper macros */

#define MOD(A,B,OP1,OP2,SZ) {                                      \
        if (op_signed(OP1) && !op_signed(OP2)) {                   \
            result = (SZ)(B) % (A);                                \
        } else if (!op_signed(OP1) && op_signed(OP2)) {            \
            result = (B) % (SZ)(A);                                \
        } else if (op_signed(OP1)  && op_signed(OP2)) {            \
            result = (SZ)(B) % (SZ)(A);                            \
        } else {                                                   \
            result = (B) % (A);                                    \
        }                                                          \
    }

#define DIV(A,B,OP1,OP2,SZ) {                                      \
        if (op_signed(OP1) && !op_signed(OP2)) {                   \
            result = (SZ)(B) / (A);                                \
        } else if (!op_signed(OP1) && op_signed(OP2)) {            \
            result = (B) / (SZ)(A);                                \
        } else if (op_signed(OP1)  && op_signed(OP2)) {            \
            result = (SZ)(B) / (SZ)(A);                            \
        } else {                                                   \
            result = (B) / (A);                                    \
        }                                                          \
    }

#define OP_R_W(d,a,p) {                         \
        (d) = (uint32) (a)[(p)++];              \
        (d) |= (uint32) (a)[(p)++] << 8u;       \
        (d) |= (uint32) (a)[(p)++] << 16u;      \
        (d) |= (uint32) (a)[(p)++] << 24u;      \
    }

#define OP_R_H(d,a,p) {                         \
        (d) = (uint16) (a)[(p)++];              \
        (d) |= (uint16) (a)[(p)++] << 8u;       \
    }

#define OP_R_B(d,a,p) {                        \
        (d) = (uint8) (a)[(p)++];              \
    }

#endif
