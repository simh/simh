/*
 * Dos/PC Emulator
 * Copyright (C) 1991 Jim Hudgens
 *
 *
 * The file is part of GDE.
 *
 * GDE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 1, or (at your option)
 * any later version.
 *
 * GDE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GDE; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* 8086 support structs and definitions */
/* definition of the registers */

/* general EAX,EBX,ECX, EDX type registers.
   Note that for portability, and speed, the issue of byte
   swapping is not addressed in the registers.  All registers
   are stored in the default format available on the
   host machine.  The only critical issue is that the
   registers should line up EXACTLY in the same manner as
   they do in the 386.  That is:

       EAX & 0xff  === AL
       EAX & 0xffff == AX

   etc.  The result is that alot of the calculations can then be
   done using the native instruction set fully.
*/

/* Endian Logic
Priority 1: If LOWFIRST is defined, use it. LOWFIRST must be 1 if the
            lower part of a 16 bit quantity comes first in memory, otherwise
            LOWFIRST must be 0
Priority 2: If __BIG_ENDIAN__ is defined, use it to define LOWFIRST accordingly
Priority 3: OS 9 on Macintosh needs LOWFIRST 0
Priority 4: Use LOWFIRST 1 as default
*/

#ifndef LOWFIRST
#ifdef __BIG_ENDIAN__
#if __BIG_ENDIAN__
#define LOWFIRST 0
#else
#define LOWFIRST 1
#endif
#elif defined (__MWERKS__) && defined (macintosh)
#define LOWFIRST 0
#else
#define LOWFIRST 1
#endif
#endif

#if LOWFIRST
typedef struct  { uint16 x_reg; }       I16_reg_t;
typedef struct  { uint8 l_reg, h_reg; } I8_reg_t;
#else
typedef struct  { uint16 x_reg; }       I16_reg_t;
typedef struct  { uint8 h_reg, l_reg; } I8_reg_t;
#endif

typedef union
{
    I16_reg_t    I16_reg;
    I8_reg_t     I8_reg;
} i386_general_register;

struct i386_general_regs
{
    i386_general_register A, B, C, D;
};

typedef struct i386_general_regs Gen_reg_t;

struct i386_special_regs
{
    i386_general_register SP, BP, SI, DI, IP;
    uint32 FLAGS;
};

/*
 *  segment registers here represent the 16 bit quantities
 *  CS, DS, ES, SS
 *
 *  segment pointers --- used to speed up  the expressions:
 *  q = m->R_CSP + m->R_IP;
 *  fetched = *q;
 *  m->R_IP += 1;
 *  compared to:
 *  fetched = GetBYTEExtended(((uint32)m->R_CS << 4) + (m->R_IP++));
 *  Save at least one shift, more if doing two byte moves.
 */
struct i386_segment_regs
{
    uint16 CS, DS, SS, ES, FS, GS;
};

/* 8 bit registers */
#define R_AH  Gn_regs.A.I8_reg.h_reg
#define R_AL  Gn_regs.A.I8_reg.l_reg
#define R_BH  Gn_regs.B.I8_reg.h_reg
#define R_BL  Gn_regs.B.I8_reg.l_reg
#define R_CH  Gn_regs.C.I8_reg.h_reg
#define R_CL  Gn_regs.C.I8_reg.l_reg
#define R_DH  Gn_regs.D.I8_reg.h_reg
#define R_DL  Gn_regs.D.I8_reg.l_reg

/* 16 bit registers */
#define R_AX  Gn_regs.A.I16_reg.x_reg
#define R_BX  Gn_regs.B.I16_reg.x_reg
#define R_CX  Gn_regs.C.I16_reg.x_reg
#define R_DX  Gn_regs.D.I16_reg.x_reg

/* special registers */
#define R_SP  Sp_regs.SP.I16_reg.x_reg
#define R_BP  Sp_regs.BP.I16_reg.x_reg
#define R_SI  Sp_regs.SI.I16_reg.x_reg
#define R_DI  Sp_regs.DI.I16_reg.x_reg
#define R_IP  Sp_regs.IP.I16_reg.x_reg
#define R_FLG Sp_regs.FLAGS

/* segment registers */
#define R_CS  Sg_regs.CS
#define R_DS  Sg_regs.DS
#define R_SS  Sg_regs.SS
#define R_ES  Sg_regs.ES

/* 8088 has top 4 bits of the flags set to 1 */
/* Also, bit#1 is set.  This is (not well) documented  behavior. */
/* see note in userman.tex about the subtleties of dealing with  */
/* code which attempts to detect the host processor.             */
/* This is def'd as F_ALWAYS_ON */
#define F_ALWAYS_ON  (0xf002)        /* flag bits always on */

/* following bits masked in to a 16bit quantity */
#define F_CF   0x1  /* CARRY flag  */
#define F_PF   0x4  /* PARITY flag */
#define F_AF  0x10  /* AUX  flag   */
#define F_ZF  0x40  /* ZERO flag   */
#define F_SF  0x80  /* SIGN flag   */
#define F_TF 0x100  /* TRAP flag   */
#define F_IF 0x200  /* INTERRUPT ENABLE flag */
#define F_DF 0x400  /* DIR flag    */
#define F_OF 0x800  /* OVERFLOW flag */

/*
 *   DEFINE A MASK FOR ONLY THOSE FLAG BITS WE WILL EVER PASS BACK
 *   (via PUSHF)
 */
#define F_MSK (F_CF|F_PF|F_AF|F_ZF|F_SF|F_TF|F_IF|F_DF|F_OF)

#define TOGGLE_FLAG(M,FLAG) (M)->R_FLG ^= FLAG
#define SET_FLAG(M,FLAG)    (M)->R_FLG |= FLAG
#define CLEAR_FLAG(M, FLAG) (M)->R_FLG &= ~FLAG
#define ACCESS_FLAG(M,FLAG) ((M)->R_FLG & (FLAG))

#define CONDITIONAL_SET_FLAG(COND,M,FLAG) \
  if (COND) SET_FLAG(M,FLAG); else CLEAR_FLAG(M,FLAG)

/* emulator machine state. */
/* segment usage control */
#define SYSMODE_SEG_DS_SS   0x01
#define SYSMODE_SEGOVR_CS   0x02
#define SYSMODE_SEGOVR_DS   0x04
#define SYSMODE_SEGOVR_ES   0x08
#define SYSMODE_SEGOVR_SS   0x10

#define SYSMODE_SEGMASK  (SYSMODE_SEG_DS_SS | SYSMODE_SEGOVR_CS |   \
    SYSMODE_SEGOVR_DS | SYSMODE_SEGOVR_ES | SYSMODE_SEGOVR_SS)

#define SYSMODE_PREFIX_REPE     0x20
#define SYSMODE_PREFIX_REPNE    0x40

#define INTR_SYNCH          0x1
#define INTR_HALTED         0x4
#define INTR_ILLEGAL_OPCODE 0x8

/* INSTRUCTION DECODING STUFF */
#define FETCH_DECODE_MODRM(m,mod,rh,rl) fetch_decode_modrm(m,&mod,&rh,&rl)
#define DECODE_RM_BYTE_REGISTER(m,r)    decode_rm_byte_register(m,r)
#define DECODE_RM_WORD_REGISTER(m,r)    decode_rm_word_register(m,r)
#define DECODE_CLEAR_SEGOVR(m)          m->sysmode &= ~(SYSMODE_SEGMASK)

typedef struct pc_env PC_ENV;
struct  pc_env
{
   /*   The registers!!     */
   struct i386_general_regs Gn_regs;
   struct i386_special_regs Sp_regs;
   struct i386_segment_regs Sg_regs;
   /* our flags structrure.  This contains information on
           REPE prefix           2 bits  repe,repne
           SEGMENT overrides     5 bits  normal,DS,SS,CS,ES
           Delayed flag set      3 bits  (zero, signed, parity)
           reserved              6 bits
           interrupt #           8 bits  instruction raised interrupt
           BIOS video segregs    4 bits
           Interrupt Pending     1 bits
           Extern interrupt      1 bits
           Halted                1 bits
    */
   long sysmode;
   uint8 intno;
};

/* GLOBAL */
extern volatile int intr;

void halt_sys (PC_ENV *sys);
void fetch_decode_modrm (PC_ENV *m, uint16 *mod, uint16 *regh, uint16 *regl);
uint8 *decode_rm_byte_register (PC_ENV *m, int reg);
uint16 *decode_rm_word_register (PC_ENV *m, int reg);
uint16 *decode_rm_seg_register (PC_ENV *m, int reg);
uint8 fetch_byte_imm (PC_ENV *m);
uint16 fetch_word_imm (PC_ENV *m);
uint16 decode_rm00_address (PC_ENV *m, int rm);
uint16 decode_rm01_address (PC_ENV *m, int rm);
uint16 decode_rm10_address (PC_ENV *m, int rm);
uint8 fetch_data_byte (PC_ENV *m, uint16 offset);
uint8 fetch_data_byte_abs (PC_ENV *m, uint16 segment, uint16 offset);
uint16 fetch_data_word (PC_ENV *m, uint16 offset);
uint16 fetch_data_word_abs (PC_ENV *m, uint16 segment, uint16 offset);
void store_data_byte (PC_ENV *m, uint16 offset, uint8 val);
void store_data_byte_abs (PC_ENV *m, uint16 segment, uint16 offset, uint8 val);
void store_data_word (PC_ENV *m, uint16 offset, uint16 val);
void store_data_word_abs (PC_ENV *m, uint16 segment, uint16 offset, uint16 val);

typedef void (*OP)(PC_ENV *m);
extern OP i86_optab[256];

/* PRIMITIVE OPERATIONS */

uint8   aad_word (PC_ENV *m, uint16 d);
uint16  aam_word (PC_ENV *m, uint8 d);
uint8   adc_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  adc_word (PC_ENV *m, uint16 d, uint16 s);
uint8   add_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  add_word (PC_ENV *m, uint16 d, uint16 s);
uint8   and_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  and_word (PC_ENV *m, uint16 d, uint16 s);
uint8   cmp_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  cmp_word (PC_ENV *m, uint16 d, uint16 s);
uint8   dec_byte (PC_ENV *m, uint8 d);
uint16  dec_word (PC_ENV *m, uint16 d);
uint8   inc_byte (PC_ENV *m, uint8 d);
uint16  inc_word (PC_ENV *m, uint16 d);
uint8   or_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  or_word (PC_ENV *m, uint16 d, uint16 s);
uint8   neg_byte (PC_ENV *m, uint8 s);
uint16  neg_word (PC_ENV *m, uint16 s);
uint8   not_byte (PC_ENV *m, uint8 s);
uint16  not_word (PC_ENV *m, uint16 s);
uint16  mem_access_word (PC_ENV *m, int addr);
void    push_word (PC_ENV *m, uint16 w);
uint16  pop_word (PC_ENV *m);
uint8   rcl_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  rcl_word (PC_ENV *m, uint16 d, uint16 s);
uint8   rcr_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  rcr_word (PC_ENV *m, uint16 d, uint16 s);
uint8   rol_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  rol_word (PC_ENV *m, uint16 d, uint16 s);
uint8   ror_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  ror_word (PC_ENV *m, uint16 d, uint16 s);
uint8   shl_byte (PC_ENV *m, uint8 d, uint8 s) ;
uint16  shl_word (PC_ENV *m, uint16 d, uint16 s);
uint8   shr_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  shr_word (PC_ENV *m, uint16 d, uint16 s);
uint8   sar_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  sar_word (PC_ENV *m, uint16 d, uint16 s);
uint8   sbb_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  sbb_word (PC_ENV *m, uint16 d, uint16 s);
uint8   sub_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  sub_word (PC_ENV *m, uint16 d, uint16 s);
void    test_byte (PC_ENV *m, uint8 d, uint8 s);
void    test_word (PC_ENV *m, uint16 d, uint16 s);
uint8   xor_byte (PC_ENV *m, uint8 d, uint8 s);
uint16  xor_word (PC_ENV *m, uint16 d, uint16 s);
void    imul_byte (PC_ENV *m, uint8 s);
void    imul_word (PC_ENV *m, uint16 s);
void    mul_byte (PC_ENV *m, uint8 s);
void    mul_word (PC_ENV *m, uint16 s);
void    idiv_byte (PC_ENV *m, uint8 s);
void    idiv_word (PC_ENV *m, uint16 s);
void    div_byte (PC_ENV *m, uint8 s);
void    div_word (PC_ENV *m, uint16 s);
