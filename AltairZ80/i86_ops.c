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

#include "altairz80_defs.h"
#include "i86.h"

extern void out(const uint32 Port, const uint32 Value);
extern uint32 in(const uint32 Port);

/* $Log: i86_ops.c,v $
 * Revision 0.11  1991/07/30  02:02:04  hudgens
 * added copyright.
 *
 * Revision 0.10  1991/07/21  18:22:08  hudgens
 * Fixed problem with scans, which was the result of the loop break
 * condition being incorrect when used in conjunction with the repe
 * or repne prefixes.  Eureka.  pkzip/pkunzip now compress/decompress
 * correctly.
 *
 * Revision 0.9  1991/07/21  03:33:18  hudgens
 * fixed popf so that it appears to be the same as an 8088 popf, and always
 * sets the high 4 bits of the flags.
 *
 * Revision 0.8  1991/07/21  01:44:11  hudgens
 * fixed aad and aam instructions.
 *
 * Revision 0.7  1991/07/21  00:31:24  hudgens
 * Fixed iret so that it works correctly.
 *
 * Revision 0.6  1991/07/20  16:54:50  hudgens
 * removed the 8087 coprocessor operations.  Moved to i87_ops.c
 *
 * Revision 0.5  1991/07/17  03:50:10  hudgens
 * Minor modifications.
 *
 * Revision 0.4  1991/06/18  02:48:41  hudgens
 * Fixed a problem with scasb and scasw.
 *
 * Revision 0.3  1991/06/03  01:01:10  hudgens
 * fixed minor problems due to unsigned to signed short integer
 * promotions.
 *
 * Revision 0.2  1991/03/31  01:32:10  hudgens
 * fixed segment handling.  Added calls to DECODE_CLEAR_SEGOVR in
 * many places in the code.  Should work much better now.
 *
 * Revision 0.1  1991/03/30  21:15:48  hudgens
 * Initial checkin to RCS.
 *
 *
 */

/* 2/23/91  fixed decode for operand x87. */

/* partially mechanically generated file....(based on the optable) */
/*
  There are approximately 250 subroutines in here, which correspond
  to the 256 byte-"opcodes" found on the 8086.  The table which
  dispatches this is found in the files optab.[ch].

  Each opcode proc has a comment preceeding it which gives it's table
  address.  Several opcodes are missing (undefined) in the table.

  Each proc includes information for decoding (DECODE_PRINTF and
  and misc functions (
  Many of the procedures are *VERY* similar in coding.  This has
  allowed for a very large amount of code to be generated in a fairly
  short amount of time (i.e. cut, paste, and modify).
  The result is that much of the code below could
  have been folded into subroutines for a large reduction in size of
  this file.  The downside would be that there would be a penalty in
  execution speed.  The file could also have been *MUCH* larger by
  inlining certain functions which were called.  This could have
  resulted even faster execution.  The prime directive I used to decide
  whether to inline the code or to modularize it, was basically: 1) no
  unnecessary subroutine calls, 2) no routines more than about 200 lines
  in size, and 3) modularize any code that I might not get right the first
  time.  The fetch_*  subroutines  fall into the latter category.  The
  The decode_* fall into the second category.  The coding of the
  "switch(mod){ .... }"  in many of the subroutines below falls into the
  first category.  Especially, the coding of {add,and,or,sub,...}_{byte,word}
  subroutines are an especially glaring case of the third guideline.
  Since so much of the code is cloned from other modules (compare
  opcode #00 to opcode #01), making the basic operations subroutine calls
  is especially important; otherwise mistakes in coding an "add"
  would represent a nightmare in maintenance.

  So, without further ado, ...
*/

extern uint8 parity_tab[];

static void i86op_illegal_op(PC_ENV *m)
{
    intr |= INTR_ILLEGAL_OPCODE;
}

/*opcode=0x00*/
static void i86op_add_byte_RM_R(PC_ENV *m)
{
   uint16     mod,rl,rh;
   uint8      *destreg,*srcreg;
   uint16     destoffset;
   uint8      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = add_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = add_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = add_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      *destreg = add_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x01*/
static void i86op_add_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16        destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = add_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = add_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = add_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      *destreg = add_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x02*/
static void i86op_add_byte_R_RM(PC_ENV *m)
{
   uint16     mod,rl,rh;
   uint8      *destreg,*srcreg;
   uint16     srcoffset;
   uint8      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = add_byte(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = add_byte(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = add_byte(m, * destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rh);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rl);
      *destreg = add_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x03*/
static void i86op_add_word_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = add_word(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = add_word(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = add_word(m, *destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = add_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x04*/
static void i86op_add_byte_AL_IMM(PC_ENV *m)
{
   uint8 srcval;
   srcval  =  fetch_byte_imm(m);
   m->R_AL  = add_byte(m, m->R_AL, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x05*/
static void i86op_add_word_AX_IMM(PC_ENV *m)
{
   uint16 srcval;
   srcval  =  fetch_word_imm(m);
   m->R_AX = add_word(m, m->R_AX, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x06*/
static void i86op_push_ES(PC_ENV *m)
{
   push_word(m,m->R_ES);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x07*/
static void i86op_pop_ES(PC_ENV *m)
{
   m->R_ES = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x08*/
static void i86op_or_byte_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      destoffset;
   uint8       destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = or_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = or_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = or_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      *destreg = or_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x09*/
static void i86op_or_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = or_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = or_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = or_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      *destreg = or_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x0a*/
static void i86op_or_byte_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      srcoffset;
   uint8       srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = or_byte(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = or_byte(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = or_byte(m, * destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rh);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rl);
      *destreg = or_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x0b*/
static void i86op_or_word_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = or_word(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = or_word(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = or_word(m, *destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = or_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x0c*/
static void i86op_or_byte_AL_IMM(PC_ENV *m)
{
   uint8 srcval;
   srcval  =  fetch_byte_imm(m);
   m->R_AL  = or_byte(m, m->R_AL, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x0d*/
static void i86op_or_word_AX_IMM(PC_ENV *m)
{
   uint16 srcval;
   srcval  =  fetch_word_imm(m);
   m->R_AX = or_word(m, m->R_AX, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x0e*/
static void i86op_push_CS(PC_ENV *m)
{
   push_word(m,m->R_CS);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x0f === ILLEGAL OP*/

/*opcode=0x10*/
static void i86op_adc_byte_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      destoffset;
   uint8       destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = adc_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = adc_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = adc_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      *destreg = adc_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x11*/
static void i86op_adc_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = adc_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = adc_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = adc_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      *destreg = adc_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x12*/
static void i86op_adc_byte_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      srcoffset;
   uint8       srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = adc_byte(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = adc_byte(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = adc_byte(m, * destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rh);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rl);
      *destreg = adc_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x13*/
static void i86op_adc_word_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = adc_word(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = adc_word(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = adc_word(m, *destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = adc_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x14*/
static void i86op_adc_byte_AL_IMM(PC_ENV *m)
{
   uint8 srcval;
   srcval  =  fetch_byte_imm(m);
   m->R_AL  = adc_byte(m, m->R_AL, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x15*/
static void i86op_adc_word_AX_IMM(PC_ENV *m)
{
   uint16 srcval;
   srcval  =  fetch_word_imm(m);
   m->R_AX = adc_word(m, m->R_AX, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x16*/
static void i86op_push_SS(PC_ENV *m)
{
   push_word(m,m->R_SS);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x17*/
static void i86op_pop_SS(PC_ENV *m)
{
   m->R_SS = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x18*/
static void i86op_sbb_byte_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      destoffset;
   uint8       destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = sbb_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = sbb_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = sbb_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      *destreg = sbb_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x19*/
static void i86op_sbb_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = sbb_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = sbb_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = sbb_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      *destreg = sbb_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x1a*/
static void i86op_sbb_byte_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      srcoffset;
   uint8       srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = sbb_byte(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = sbb_byte(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = sbb_byte(m, * destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rh);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rl);
      *destreg = sbb_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x1b*/
static void i86op_sbb_word_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = sbb_word(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = sbb_word(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = sbb_word(m, *destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = sbb_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x1c*/
static void i86op_sbb_byte_AL_IMM(PC_ENV *m)
{
   uint8 srcval;
   srcval  =  fetch_byte_imm(m);
   m->R_AL  = sbb_byte(m, m->R_AL, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x1d*/
static void i86op_sbb_word_AX_IMM(PC_ENV *m)
{
   uint16 srcval;
   srcval  =  fetch_word_imm(m);
   m->R_AX = sbb_word(m, m->R_AX, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x1e*/
static void i86op_push_DS(PC_ENV *m)
{
   push_word(m,m->R_DS);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x1f*/
static void i86op_pop_DS(PC_ENV *m)
{
   m->R_DS = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x20*/
static void i86op_and_byte_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      destoffset;
   uint8       destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = and_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = and_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = and_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      *destreg = and_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x21*/
static void i86op_and_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = and_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = and_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = and_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      *destreg = and_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x22*/
static void i86op_and_byte_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      srcoffset;
   uint8       srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = and_byte(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = and_byte(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = and_byte(m, * destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rh);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rl);
      *destreg = and_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x23*/
static void i86op_and_word_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = and_word(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = and_word(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = and_word(m, *destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = and_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x24*/
static void i86op_and_byte_AL_IMM(PC_ENV *m)
{
   uint8 srcval;
   srcval  =  fetch_byte_imm(m);
   m->R_AL  = and_byte(m, m->R_AL, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x25*/
static void i86op_and_word_AX_IMM(PC_ENV *m)
{
   uint16 srcval;
   srcval  =  fetch_word_imm(m);
   m->R_AX = and_word(m, m->R_AX, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x26*/
static void i86op_segovr_ES(PC_ENV *m)
{
   m->sysmode |= SYSMODE_SEGOVR_ES;
   /*   note the lack of DECODE_CLEAR_SEGOVR(r)
    since, here is one of 4 opcode subroutines we do not
    want to do this.
    */
}

/*opcode=0x27*/
static void i86op_daa(PC_ENV *m)
{
   uint16 dbyte;
   dbyte = m->R_AL;
   if (ACCESS_FLAG(m,F_AF)|| (dbyte&0xf) > 9)
     {
    dbyte += 6;
    if (dbyte&0x100)
      SET_FLAG(m, F_CF);
    SET_FLAG(m, F_AF);
     }
   else
     CLEAR_FLAG(m, F_AF);
   if (ACCESS_FLAG(m,F_CF) || (dbyte&0xf0) > 0x90)
     {
    dbyte += 0x60;
    SET_FLAG(m, F_CF);
     }
   else
     CLEAR_FLAG(m, F_CF);
   m->R_AL = (uint8) dbyte;
   CONDITIONAL_SET_FLAG((m->R_AL & 0x80),m,F_SF);
   CONDITIONAL_SET_FLAG((m->R_AL == 0), m,F_ZF);
   CONDITIONAL_SET_FLAG((parity_tab[m->R_AL]),m,F_PF);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x28*/
static void i86op_sub_byte_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      destoffset;
   uint8       destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = sub_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = sub_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = sub_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      *destreg = sub_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x29*/
static void i86op_sub_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = sub_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = sub_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = sub_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      *destreg = sub_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x2a*/
static void i86op_sub_byte_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      srcoffset;
   uint8       srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = sub_byte(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = sub_byte(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = sub_byte(m, * destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rh);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rl);
      *destreg = sub_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x2b*/
static void i86op_sub_word_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = sub_word(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = sub_word(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = sub_word(m, *destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = sub_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x2c*/
static void i86op_sub_byte_AL_IMM(PC_ENV *m)
{
   uint8 srcval;
   srcval  =  fetch_byte_imm(m);
   m->R_AL  = sub_byte(m, m->R_AL, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x2d*/
static void i86op_sub_word_AX_IMM(PC_ENV *m)
{
   uint16 srcval;
   srcval  =  fetch_word_imm(m);
   m->R_AX = sub_word(m, m->R_AX, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x2e*/
static void i86op_segovr_CS(PC_ENV *m)
{
   m->sysmode |= SYSMODE_SEGOVR_CS;
   /* note no DECODE_CLEAR_SEGOVER here. */
}

/*opcode=0x2f*/
static void i86op_das(PC_ENV *m)
{
   uint16 dbyte;
   dbyte = m->R_AL;
   if ( ACCESS_FLAG(m,F_AF) || (dbyte&0xf) > 9)
     {
    dbyte -= 6;
    if (dbyte&0x100)            /* XXXXX --- this is WRONG */
      SET_FLAG(m, F_CF);
    SET_FLAG(m, F_AF);
     }
   else
     CLEAR_FLAG(m, F_AF);
   if (ACCESS_FLAG(m,F_CF) || (dbyte&0xf0) > 0x90)
     {
    dbyte -= 0x60;
    SET_FLAG(m, F_CF);
     }
   else
     CLEAR_FLAG(m, F_CF);
   m->R_AL = (uint8) dbyte;
   CONDITIONAL_SET_FLAG(m->R_AL & 0x80,m,F_SF);
   CONDITIONAL_SET_FLAG(m->R_AL == 0,m,F_ZF);
   CONDITIONAL_SET_FLAG(parity_tab[m->R_AL],m,F_PF);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x30*/
static void i86op_xor_byte_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      destoffset;
   uint8       destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = xor_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = xor_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      destval = xor_byte(m, destval, *srcreg);
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      *destreg = xor_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x31*/
static void i86op_xor_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = xor_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = xor_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destval = xor_word(m, destval, *srcreg);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      *destreg = xor_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x32*/
static void i86op_xor_byte_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      srcoffset;
   uint8       srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = xor_byte(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = xor_byte(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = xor_byte(m, *destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rh);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rl);
      *destreg = xor_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x33*/
static void i86op_xor_word_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = xor_word(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = xor_word(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = xor_word(m, *destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = xor_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x34*/
static void i86op_xor_byte_AL_IMM(PC_ENV *m)
{
   uint8 srcval;
   srcval  =  fetch_byte_imm(m);
   m->R_AL  = xor_byte(m, m->R_AL, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x35*/
static void i86op_xor_word_AX_IMM(PC_ENV *m)
{
   uint16 srcval;
   srcval  =  fetch_word_imm(m);
   m->R_AX = xor_word(m, m->R_AX, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x36*/
static void i86op_segovr_SS(PC_ENV *m)
{
   m->sysmode |= SYSMODE_SEGOVR_SS;
   /* no DECODE_CLEAR_SEGOVER ! */
}

/*opcode=0x37*/
static void i86op_aaa(PC_ENV *m)
{
   if ( (m->R_AL & 0xf) > 0x9 || ACCESS_FLAG(m,F_AF))
       {
     m->R_AL += 0x6;
     m->R_AH += 1;
     SET_FLAG(m, F_AF);
     SET_FLAG(m, F_CF);
       }
   else
       {
      CLEAR_FLAG(m, F_CF);
      CLEAR_FLAG(m, F_AF);
       }
   m->R_AL &= 0xf;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x38*/
static void i86op_cmp_byte_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      destoffset;
   uint8         destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      cmp_byte(m, destval, *srcreg);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      cmp_byte(m, destval, *srcreg);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      cmp_byte(m, destval, *srcreg);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      cmp_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x39*/
static void i86op_cmp_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      cmp_word(m, destval, *srcreg);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      cmp_word(m, destval, *srcreg);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      cmp_word(m, destval, *srcreg);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      cmp_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x3a*/
static void i86op_cmp_byte_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      srcoffset;
   uint8       srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      cmp_byte(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      cmp_byte(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      cmp_byte(m, * destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rh);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rl);
      cmp_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x3b*/
static void i86op_cmp_word_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      cmp_word(m, *destreg, srcval);
      break;
    case 1:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      cmp_word(m, *destreg, srcval);
      break;
    case 2:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      cmp_word(m, *destreg, srcval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      cmp_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x3c*/
static void i86op_cmp_byte_AL_IMM(PC_ENV *m)
{
   uint8 srcval;
   srcval  =  fetch_byte_imm(m);
   cmp_byte(m, m->R_AL, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x3d*/
static void i86op_cmp_word_AX_IMM(PC_ENV *m)
{
   uint16 srcval;
   srcval  =  fetch_word_imm(m);
   cmp_word(m, m->R_AX, srcval);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x3e*/
static void i86op_segovr_DS(PC_ENV *m)
{
   m->sysmode |= SYSMODE_SEGOVR_DS;
   /* NO DECODE_CLEAR_SEGOVR! */
}

/*opcode=0x3f*/
static void i86op_aas(PC_ENV *m)
{
  /* ????  Check out the subtraction here.  Will this ?ever? cause
     the contents of R_AL or R_AH to be affected incorrectly since
     they are being subtracted from *and* are unsigned.
     Should put an assertion in here.
   */
   if ( (m->R_AL & 0xf) > 0x9 || ACCESS_FLAG(m,F_AF))
       {
      m->R_AL -= 0x6;
      m->R_AH -= 1;
      SET_FLAG(m, F_AF);
      SET_FLAG(m, F_CF);
       }
   else
       {
      CLEAR_FLAG(m, F_CF);
      CLEAR_FLAG(m, F_AF);
       }
   m->R_AL &= 0xf;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x40*/
static void i86op_inc_AX(PC_ENV *m)
{
   m->R_AX = inc_word(m,m->R_AX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x41*/
static void i86op_inc_CX(PC_ENV *m)
{
   m->R_CX = inc_word(m,m->R_CX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x42*/
static void i86op_inc_DX(PC_ENV *m)
{
   m->R_DX = inc_word(m,m->R_DX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x43*/
static void i86op_inc_BX(PC_ENV *m)
{
   m->R_BX = inc_word(m,m->R_BX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x44*/
static void i86op_inc_SP(PC_ENV *m)
{
   m->R_SP = inc_word(m,m->R_SP);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x45*/
static void i86op_inc_BP(PC_ENV *m)
{
   m->R_BP = inc_word(m,m->R_BP);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x46*/
static void i86op_inc_SI(PC_ENV *m)
{
   m->R_SI = inc_word(m,m->R_SI);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x47*/
static void i86op_inc_DI(PC_ENV *m)
{
   m->R_DI = inc_word(m,m->R_DI);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x48*/
static void i86op_dec_AX(PC_ENV *m)
{
   m->R_AX = dec_word(m,m->R_AX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x49*/
static void i86op_dec_CX(PC_ENV *m)
{
   m->R_CX = dec_word(m,m->R_CX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x4a*/
static void i86op_dec_DX(PC_ENV *m)
{
   m->R_DX = dec_word(m,m->R_DX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x4b*/
static void i86op_dec_BX(PC_ENV *m)
{
   m->R_BX = dec_word(m,m->R_BX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x4c*/
static void i86op_dec_SP(PC_ENV *m)
{
   m->R_SP = dec_word(m,m->R_SP);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x4d*/
static void i86op_dec_BP(PC_ENV *m)
{
   m->R_BP = dec_word(m,m->R_BP);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x4e*/
static void i86op_dec_SI(PC_ENV *m)
{
   m->R_SI = dec_word(m,m->R_SI);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x4f*/
static void i86op_dec_DI(PC_ENV *m)
{
   m->R_DI = dec_word(m,m->R_DI);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x50*/
static void i86op_push_AX(PC_ENV *m)
{
   push_word(m,m->R_AX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x51*/
static void i86op_push_CX(PC_ENV *m)
{
   push_word(m,m->R_CX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x52*/
static void i86op_push_DX(PC_ENV *m)
{
   push_word(m,m->R_DX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x53*/
static void i86op_push_BX(PC_ENV *m)
{
   push_word(m,m->R_BX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x54*/
static void i86op_push_SP(PC_ENV *m)
{
   /* ....  Note this weirdness: One book I have access to
    claims that the value pushed here is actually sp-2.  I.e.
    it decrements the stackpointer, and then pushes it.  The 286
    I have does it this way.  Changing this causes many problems.*/
   /* changed to push SP-2, since this *IS* how a 8088 does this */
   push_word(m,m->R_SP-2);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x55*/
static void i86op_push_BP(PC_ENV *m)
{
   push_word(m,m->R_BP);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x56*/
static void i86op_push_SI(PC_ENV *m)
{
   push_word(m,m->R_SI);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x57*/
static void i86op_push_DI(PC_ENV *m)
{
   push_word(m,m->R_DI);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x58*/
static void i86op_pop_AX(PC_ENV *m)
{
   m->R_AX = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x59*/
static void i86op_pop_CX(PC_ENV *m)
{
   m->R_CX = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x5a*/
static void i86op_pop_DX(PC_ENV *m)
{
   m->R_DX = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x5b*/
static void i86op_pop_BX(PC_ENV *m)
{
   m->R_BX = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x5c*/
static void i86op_pop_SP(PC_ENV *m)
{
   m->R_SP = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x5d*/
static void i86op_pop_BP(PC_ENV *m)
{
   m->R_BP = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x5e*/
static void i86op_pop_SI(PC_ENV *m)
{
   m->R_SI = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x5f*/
static void i86op_pop_DI(PC_ENV *m)
{
   m->R_DI = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x60   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x61   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x62   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x63   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x64   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x65   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x66   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x67   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x68   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x69   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x6a   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x6b   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x6c   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x6d   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x6e   ILLEGAL OP, calls i86op_illegal_op() */
/*opcode=0x6f   ILLEGAL OP, calls i86op_illegal_op() */

/*opcode=0x70*/
static void i86op_jump_near_O(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if overflow flag is set */
   offset = (int8) fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (ACCESS_FLAG(m,F_OF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x71*/
static void i86op_jump_near_NO(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if overflow is not set */
   offset = (int8) fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (!ACCESS_FLAG(m,F_OF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x72*/
static void i86op_jump_near_B(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if carry flag is set. */
   offset = (int8)fetch_byte_imm(m);   /* sign extended ??? */
   target = (int16)(m->R_IP) + offset;
   if (ACCESS_FLAG(m, F_CF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x73*/
static void i86op_jump_near_NB(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if carry flag is clear. */
   offset = (int8)fetch_byte_imm(m);   /* sign extended ??? */
   target = (int16)(m->R_IP) + offset;
   if (!ACCESS_FLAG(m,F_CF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x74*/
static void i86op_jump_near_Z(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if zero flag is set. */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (ACCESS_FLAG(m, F_ZF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x75*/
static void i86op_jump_near_NZ(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if zero flag is clear. */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (!ACCESS_FLAG(m, F_ZF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x76*/
static void i86op_jump_near_BE(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if carry flag is set or if the zero
      flag is set. */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (ACCESS_FLAG(m,F_CF) || ACCESS_FLAG(m,F_ZF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x77*/
static void i86op_jump_near_NBE(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if carry flag is clear and if the zero
      flag is clear */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (!(ACCESS_FLAG(m,F_CF)||ACCESS_FLAG(m,F_ZF)))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x78*/
static void i86op_jump_near_S(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if sign flag is set */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (ACCESS_FLAG(m,F_SF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x79*/
static void i86op_jump_near_NS(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if sign flag is clear */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (!ACCESS_FLAG(m,F_SF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x7a*/
static void i86op_jump_near_P(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if parity flag is set (even parity) */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (ACCESS_FLAG(m, F_PF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x7b*/
static void i86op_jump_near_NP(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   /* jump to byte offset if parity flag is clear (odd parity) */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   if (!ACCESS_FLAG(m, F_PF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/* JHH fixed till here... */

/*opcode=0x7c*/
static void i86op_jump_near_L(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   int   sf,of;
   /* jump to byte offset if sign flag not equal to overflow flag. */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   /* note:
    *  this is the simplest expression i could think of which
    *  expresses SF != OF.  m->R_FLG&F_SF either equals x80 or x00.
    *  Similarly m->R_FLG&F_OF either equals x800 or x000.
    *  The former shifted right by 7 puts a 1 or 0 in bit 0.
    *  The latter shifter right by 11 puts a 1 or 0 in bit 0.
    *  if the two expressions are the same, i.e. equal, then
    *  a zero results from the xor.  If they are not equal,
    *  then a 1 results, and the jump is taken.
    */
   sf = ACCESS_FLAG(m,F_SF) != 0;
   of = ACCESS_FLAG(m,F_OF) != 0;
   /* was: if ( ((m->R_FLG & F_SF)>>7) ^ ((m->R_FLG & F_OF) >> 11))*/
   if (sf ^ of)
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x7d*/
static void i86op_jump_near_NL(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   int sf,of;
   /* jump to byte offset if sign flag not equal to overflow flag. */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   sf = ACCESS_FLAG(m,F_SF) != 0;
   of = ACCESS_FLAG(m,F_OF) != 0;
   /* note: inverse of above, but using == instead of xor. */
   /* was: if (((m->R_FLG & F_SF)>>7) == ((m->R_FLG & F_OF) >> 11))*/
   if (sf == of)
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x7e*/
static void i86op_jump_near_LE(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   int sf,of;
   /* jump to byte offset if sign flag not equal to overflow flag
    or the zero flag is set */
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   sf = ACCESS_FLAG(m,F_SF) != 0;
   of = ACCESS_FLAG(m,F_OF) != 0;
   /* note: modification of JL */
   /* sf != of */
   /* was:  if ((((m->R_FLG & F_SF)>>7) ^ ((m->R_FLG & F_OF) >> 11))
     || (m->R_FLG & F_ZF) ) */
   if ( (sf ^ of) || ACCESS_FLAG(m,F_ZF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x7f*/
static void i86op_jump_near_NLE(PC_ENV *m)
{
   int8  offset;
   uint16 target;
   int sf,of;
   /* jump to byte offset if sign flag equal to overflow flag.
    and the zero flag is clear*/
   offset = (int8)fetch_byte_imm(m);
   target = (int16)(m->R_IP) + offset;
   sf = ACCESS_FLAG(m,F_SF) != 0;
   of = ACCESS_FLAG(m,F_OF) != 0;

/*   if (((m->R_FLG & F_SF)>>7) == ((m->R_FLG & F_OF) >> 11)
       && (!(m->R_FLG & F_ZF))) */
   if ( ( sf == of ) && !ACCESS_FLAG(m,F_ZF))
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

static uint8    (*opc80_byte_operation[])(PC_ENV *m,uint8 d,uint8 s) =
{
    add_byte,/*00*/
    or_byte, /*01*/
    adc_byte,/*02*/
    sbb_byte,/*03*/
    and_byte,/*04*/
    sub_byte,/*05*/
    xor_byte,/*06*/
    cmp_byte,/*07*/
};

/*opcode=0x80*/
static void i86op_opc80_byte_RM_IMM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg;
   uint16      destoffset;
   uint8       imm;
   uint8       destval;
   /* weirdo special case instruction format.  Part of the
      opcode held below in "RH".  Doubly nested case would
      result, except that the decoded instruction
    */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   /* know operation, decode the mod byte to find the addressing
      mode. */
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      imm  =  fetch_byte_imm(m);
      destval = (*opc80_byte_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      imm  =  fetch_byte_imm(m);
      destval = (*opc80_byte_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      imm  =  fetch_byte_imm(m);
      destval = (*opc80_byte_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      imm  =  fetch_byte_imm(m);
      destval = (*opc80_byte_operation[rh])(m, *destreg, imm);
      if (rh != 7)
          *destreg = destval;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

static uint16    (*opc81_word_operation[])(PC_ENV *m,uint16 d,uint16 s) =
{    add_word,/*00*/
    or_word, /*01*/
    adc_word,/*02*/
    sbb_word,/*03*/
    and_word,/*04*/
    sub_word,/*05*/
    xor_word,/*06*/
    cmp_word,/*07*/
};

/*opcode=0x81*/
static void i86op_opc81_word_RM_IMM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg;
   uint16      destoffset;
   uint16      imm;
   uint16      destval;
   /* weirdo special case instruction format.  Part of the
      opcode held below in "RH".  Doubly nested case would
      result, except that the decoded instruction
    */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   /* know operation, decode the mod byte to find the addressing
      mode. */
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      imm  =  fetch_word_imm(m);
      destval = (*opc81_word_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      imm  =  fetch_word_imm(m);
      destval = (*opc81_word_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      imm  =  fetch_word_imm(m);
      destval = (*opc81_word_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      imm  =  fetch_word_imm(m);
      destval = (*opc81_word_operation[rh])(m, *destreg, imm);
      if (rh != 7)
          *destreg = destval;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
    }

static uint8    (*opc82_byte_operation[])(PC_ENV *m,uint8 s,uint8 d) =
{
    add_byte,/*00*/
    or_byte, /*01*/  /*YYY UNUSED ????*/
    adc_byte,/*02*/
    sbb_byte,/*03*/
    and_byte,/*04*/  /*YYY UNUSED ????*/
    sub_byte,/*05*/
    xor_byte,/*06*/  /*YYY UNUSED ????*/
    cmp_byte,/*07*/
};

/*opcode=0x82*/
static void i86op_opc82_byte_RM_IMM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg;
   uint16      destoffset;
   uint8       imm;
   uint8       destval;
   /* weirdo special case instruction format.  Part of the
      opcode held below in "RH".  Doubly nested case would
      result, except that the decoded instruction
      Similar to opcode 81, except that the immediate byte
      is sign extended to a word length.
    */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   /* know operation, decode the mod byte to find the addressing
      mode. */
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      imm  = fetch_byte_imm(m);
      destval = (*opc82_byte_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      imm  =  fetch_byte_imm(m);
      destval = (*opc82_byte_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      imm  =  fetch_byte_imm(m);
      destval = (*opc82_byte_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      imm  =  fetch_byte_imm(m);
      destval = (*opc82_byte_operation[rh])(m, *destreg, imm);
      if (rh != 7)
          *destreg = destval;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

static uint16 (*opc83_word_operation[])(PC_ENV *m,uint16 s,uint16 d) =
{
    add_word,/*00*/
    or_word, /*01*/  /*YYY UNUSED ????*/
    adc_word,/*02*/
    sbb_word,/*03*/
    and_word,/*04*/  /*YYY UNUSED ????*/
    sub_word,/*05*/
    xor_word,/*06*/  /*YYY UNUSED ????*/
    cmp_word,/*07*/
};

/*opcode=0x83*/
static void i86op_opc83_word_RM_IMM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg;
   uint16      destoffset;
   uint16      imm;
   uint16      destval;
   /* weirdo special case instruction format.  Part of the
      opcode held below in "RH".  Doubly nested case would
      result, except that the decoded instruction
      Similar to opcode 81, except that the immediate byte
      is sign extended to a word length.
    */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   /* know operation, decode the mod byte to find the addressing
      mode. */
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      imm  = (int8)fetch_byte_imm(m);
      destval = (*opc83_word_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      imm  =  (int8)fetch_byte_imm(m);
      destval = (*opc83_word_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      imm  =  (int8) fetch_byte_imm(m);
      destval = (*opc83_word_operation[rh])(m, destval, imm);
      if (rh != 7)
          store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      imm  = (int8) fetch_byte_imm(m);
      destval = (*opc83_word_operation[rh])(m, *destreg, imm);
      if (rh != 7)
          *destreg = destval;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x84*/
static void i86op_test_byte_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      destoffset;
   uint8       destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      test_byte(m, destval, *srcreg);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      test_byte(m, destval, *srcreg);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      test_byte(m, destval, *srcreg);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      test_byte(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
    }

/*opcode=0x85*/
static void i86op_test_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      test_word(m, destval, *srcreg);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      test_word(m, destval, *srcreg);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      test_word(m, destval, *srcreg);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      test_word(m, *destreg, *srcreg);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x86*/
static void i86op_xchg_byte_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      destoffset;
   uint8       destval;
   uint8       tmp;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      tmp = *srcreg;
      *srcreg = destval;
      destval = tmp;
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      tmp = *srcreg;
      *srcreg = destval;
      destval = tmp;
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      tmp = *srcreg;
      *srcreg = destval;
      destval = tmp;
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      tmp = *srcreg;
      *srcreg = *destreg;
      *destreg = tmp;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x87*/
static void i86op_xchg_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   uint16      tmp;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      tmp = *srcreg;
      *srcreg = destval;
      destval = tmp;
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      tmp = *srcreg;
      *srcreg = destval;
      destval = tmp;
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      tmp = *srcreg;
      *srcreg = destval;
      destval = tmp;
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      tmp = *srcreg;
      *srcreg = *destreg;
      *destreg = tmp;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
    }

/*opcode=0x88*/
static void i86op_mov_byte_RM_R(PC_ENV *m)
{
   uint16       mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16       destoffset;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      store_data_byte(m,destoffset,*srcreg);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      store_data_byte(m,destoffset,*srcreg);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      store_data_byte(m,destoffset,*srcreg);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      *destreg = *srcreg;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x89*/
static void i86op_mov_word_RM_R(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      store_data_word(m,destoffset,*srcreg);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      store_data_word(m,destoffset,*srcreg);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      store_data_word(m,destoffset,*srcreg);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      *destreg = *srcreg;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x8a*/
static void i86op_mov_byte_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg,*srcreg;
   uint16      srcoffset;
   uint8       srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = srcval;
      break;
    case 1:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = srcval;
      break;
    case 2:
      destreg  =  DECODE_RM_BYTE_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_byte(m,srcoffset);
      *destreg = srcval;
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rh);
      srcreg  =  DECODE_RM_BYTE_REGISTER(m,rl);
      *destreg = *srcreg;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

 /*opcode=0x8b*/
static void i86op_mov_word_R_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = srcval;
      break;
    case 1:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = srcval;
      break;
    case 2:
      destreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = srcval;
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = *srcreg;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x8c*/
static void i86op_mov_word_RM_SR(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      srcreg  =  decode_rm_seg_register(m,rh);
      destval = *srcreg;
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      srcreg  =  decode_rm_seg_register(m,rh);
      destval = *srcreg;
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      srcreg  =  decode_rm_seg_register(m,rh);
      destval = *srcreg;
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      srcreg  =  decode_rm_seg_register(m,rh);
      *destreg = *srcreg;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x8d*/
static void i86op_lea_word_R_M(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *srcreg;
   uint16      destoffset;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destoffset=decode_rm00_address(m,rl);
      *srcreg = destoffset;
      break;
    case 1:
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destoffset=decode_rm01_address(m,rl);
      *srcreg = destoffset;
      break;
    case 2:
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      destoffset=decode_rm10_address(m,rl);
      *srcreg = destoffset;
      break;
    case 3:   /* register to register */
      /* undefined.  Do nothing. */
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x8e*/
static void i86op_mov_word_SR_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg,*srcreg;
   uint16      srcoffset;
   uint16      srcval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destreg  =  decode_rm_seg_register(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = srcval;
      break;
    case 1:
      destreg  =  decode_rm_seg_register(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = srcval;
      break;
    case 2:
      destreg  =  decode_rm_seg_register(m,rh);
      srcoffset = decode_rm10_address(m,rl);
      srcval = fetch_data_word(m,srcoffset);
      *destreg = srcval;
      break;
    case 3:   /* register to register */
      destreg  = decode_rm_seg_register(m,rh);
      srcreg  =  DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = *srcreg;
      break;
       }
   /*\
    *  clean up, and reset all the R_xSP pointers to the correct
    *  locations.  This is about 3x too much overhead (doing all the
    *  segreg ptrs when only one is needed, but this instruction
    *  *cannot* be that common, and this isn't too much work anyway.
   \*/
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x8f*/
static void i86op_pop_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg;
   uint16      destoffset;
   uint16      destval;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   if (rh != 0)
       {
      halt_sys(m);
       }
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = pop_word( m);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = pop_word(m);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = pop_word(m);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = pop_word(m);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x90*/
static void i86op_nop(PC_ENV *m)
{
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x91*/
static void i86op_xchg_word_AX_CX(PC_ENV *m)
{
   uint16 tmp;
   tmp = m->R_AX;
   m->R_AX = m->R_CX;
   m->R_CX = tmp;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x92*/
static void i86op_xchg_word_AX_DX(PC_ENV *m)
{
   uint16 tmp;
   tmp = m->R_AX;
   m->R_AX = m->R_DX;
   m->R_DX = tmp;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x93*/
static void i86op_xchg_word_AX_BX(PC_ENV *m)
{
   uint16 tmp;
   tmp = m->R_AX;
   m->R_AX = m->R_BX;
   m->R_BX = tmp;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x94*/
static void i86op_xchg_word_AX_SP(PC_ENV *m)
{
   uint16 tmp;
   tmp = m->R_AX;
   m->R_AX = m->R_SP;
   m->R_SP = tmp;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x95*/
static void i86op_xchg_word_AX_BP(PC_ENV *m)
{
   uint16 tmp;
   tmp = m->R_AX;
   m->R_AX = m->R_BP;
   m->R_BP = tmp;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x96*/
static void i86op_xchg_word_AX_SI(PC_ENV *m)
{
   uint16 tmp;
   tmp = m->R_AX;
   m->R_AX = m->R_SI;
   m->R_SI = tmp;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x97*/
static void i86op_xchg_word_AX_DI(PC_ENV *m)
{
   uint16 tmp;
   tmp = m->R_AX;
   m->R_AX = m->R_DI;
   m->R_DI = tmp;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x98*/
static void i86op_cbw(PC_ENV *m)
{
   if (m->R_AL & 0x80)
       {
      m->R_AH = 0xff;
       }
   else
       {
      m->R_AH = 0x0;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x99*/
static void i86op_cwd(PC_ENV *m)
{
   if (m->R_AX & 0x8000)
       {
      m->R_DX = 0xffff;
       }
   else
       {
      m->R_DX = 0x0;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x9a*/
static void i86op_call_far_IMM(PC_ENV *m)
{
   uint16 farseg,faroff;
   faroff = fetch_word_imm(m);
   farseg = fetch_word_imm(m);
   /* XXX
      HOOKED INTERRUPT VECTORS CALLING INTO OUR "BIOS"
      WILL CAUSE PROBLEMS UNLESS ALL INTERSEGMENT STUFF IS
      CHECKED FOR BIOS ACCESS.  CHECK NEEDED HERE.
      FOR MOMENT, LET IT ALONE.
    */
   push_word(m,m->R_CS);
   m->R_CS = farseg;
   push_word(m,m->R_IP);
   m->R_IP = faroff;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x9b*/
static void i86op_wait(PC_ENV *m)
{
   /* NADA.  */
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x9c*/
static void i86op_pushf_word(PC_ENV *m)
{
   uint16 flags;
   flags = m->R_FLG;
   /* clear out *all* bits not representing flags */
   flags &= F_MSK;
   /* TURN ON CHARACTERISTIC BITS OF FLAG FOR 8088 */
   flags |= F_ALWAYS_ON;
   push_word(m,flags);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x9d*/
static void i86op_popf_word(PC_ENV *m)
{
   m->R_FLG = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x9e*/
static void i86op_sahf(PC_ENV *m)
{
   /* clear the lower bits of the flag register */
   m->R_FLG &= 0xffffff00;
   /* or in the AH register into the flags register */
   m->R_FLG |= m->R_AH;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0x9f*/
static void i86op_lahf(PC_ENV *m)
{
   m->R_AH  = m->R_FLG & 0xff;
   /*undocumented TC++ behavior??? Nope.  It's documented, but
    you have too look real hard to notice it. */
   m->R_AH  |= 0x2;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xa0*/
static void i86op_mov_AL_M_IMM(PC_ENV *m)
{
   uint16      offset;
   uint8       destval;
   offset = fetch_word_imm(m);
   destval = fetch_data_byte(m,offset);
   m->R_AL  = destval;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xa1*/
static void i86op_mov_AX_M_IMM(PC_ENV *m)
{
   uint16  offset;
   uint16  destval;
   offset = fetch_word_imm(m);
   destval = fetch_data_word(m,offset);
   m->R_AX  = destval;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xa2*/
static void i86op_mov_M_AL_IMM(PC_ENV *m)
{
   uint16  offset;
   offset = fetch_word_imm(m);
   store_data_byte(m,offset,m->R_AL);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xa3*/
static void i86op_mov_M_AX_IMM(PC_ENV *m)
{
   uint16  offset;
   offset = fetch_word_imm(m);
   store_data_word(m,offset,m->R_AX);
   DECODE_CLEAR_SEGOVR(m);
}

/* JHH CLEANED */

/*opcode=0xa4*/
static void i86op_movs_byte(PC_ENV *m)
{
   uint8 val;
   int  inc;
   if (ACCESS_FLAG(m,F_DF)) /* down */
     inc = -1;
   else
     inc = 1;
   if (m->sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE))
       {
      /* dont care whether REPE or REPNE */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val = fetch_data_byte(m,m->R_SI);
           store_data_byte_abs(m,m->R_ES,m->R_DI,val);
           m->R_CX -= 1;
           m->R_SI += inc;
           m->R_DI += inc;
        }
      m->sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
       }
   else
       {
      val = fetch_data_byte(m,m->R_SI);
      store_data_byte_abs(m,m->R_ES,m->R_DI,val);
      m->R_SI += inc;
      m->R_DI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xa5*/
static void i86op_movs_word(PC_ENV *m)
{
   int16 val;
   int  inc;
   if (ACCESS_FLAG(m, F_DF)) /* down */
     inc = -2;
   else
     inc = 2;
   if (m->sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE))
       {
      /* dont care whether REPE or REPNE */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val = fetch_data_word(m,m->R_SI);
           store_data_word_abs(m,m->R_ES,m->R_DI,val);
           m->R_CX -= 1;
           m->R_SI += inc;
           m->R_DI += inc;
        }
      m->sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
       }
   else
       {
      val = fetch_data_word(m,m->R_SI);
      store_data_word_abs(m,m->R_ES,m->R_DI,val);
      m->R_SI += inc;
      m->R_DI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xa6*/
static void i86op_cmps_byte(PC_ENV *m)
{
   int8 val1,val2;
   int  inc;
   if (ACCESS_FLAG(m,F_DF)) /* down */
     inc = -1;
   else
     inc = 1;
   if (m->sysmode & SYSMODE_PREFIX_REPE)
       {
      /* REPE  */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val1 = fetch_data_byte(m,m->R_SI);
           val2 = fetch_data_byte_abs(m,m->R_ES,m->R_DI);
           cmp_byte(m, val1,val2);
           m->R_CX -= 1;
           m->R_SI += inc;
           m->R_DI += inc;
           if (ACCESS_FLAG(m,F_ZF)==0)
               break;
        }
      m->sysmode &= ~SYSMODE_PREFIX_REPE;
       }
   else if (m->sysmode & SYSMODE_PREFIX_REPNE)
       {
      /* REPNE  */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val1 = fetch_data_byte(m,m->R_SI);
           val2 = fetch_data_byte_abs(m,m->R_ES,m->R_DI);
           cmp_byte(m, val1,val2);
           m->R_CX -= 1;
           m->R_SI += inc;
           m->R_DI += inc;
           if (ACCESS_FLAG(m,F_ZF))
               break;  /* zero flag set means equal */
        }
      m->sysmode &= ~SYSMODE_PREFIX_REPNE;
       }
   else
       {
      val1 = fetch_data_byte(m,m->R_SI);
      val2 = fetch_data_byte_abs(m,m->R_ES,m->R_DI);
      cmp_byte(m, val1,val2);
      m->R_SI += inc;
      m->R_DI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xa7*/
static void i86op_cmps_word(PC_ENV *m)
{
   int16 val1,val2;
   int  inc;
   if (ACCESS_FLAG(m,F_DF)) /* down */
     inc = -2;
   else
     inc = 2;
   if (m->sysmode & SYSMODE_PREFIX_REPE)
       {
      /* REPE  */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val1 = fetch_data_word(m,m->R_SI);
           val2 = fetch_data_word_abs(m,m->R_ES,m->R_DI);
           cmp_word(m, val1,val2);
           m->R_CX -= 1;
           m->R_SI += inc;
           m->R_DI += inc;
           if (ACCESS_FLAG(m,F_ZF)==0)
               break;
        }
      m->sysmode &= ~SYSMODE_PREFIX_REPE;
       }
   else if (m->sysmode & SYSMODE_PREFIX_REPNE)
       {
      /* REPNE  */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val1 = fetch_data_word(m,m->R_SI);
           val2 = fetch_data_word_abs(m,m->R_ES,m->R_DI);
           cmp_word(m, val1,val2);
           m->R_CX -= 1;
           m->R_SI += inc;
           m->R_DI += inc;
           if (ACCESS_FLAG(m,F_ZF))
               break;  /* zero flag set means equal */
        }
      m->sysmode &= ~SYSMODE_PREFIX_REPNE;
       }
   else
       {
      val1 = fetch_data_word(m,m->R_SI);
      val2 = fetch_data_word_abs(m,m->R_ES,m->R_DI);
      cmp_word(m, val1,val2);
      m->R_SI += inc;
      m->R_DI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xa8*/
static void i86op_test_AL_IMM(PC_ENV *m)
{
   int  imm;
   imm = fetch_byte_imm(m);
   test_byte(m, m->R_AL, imm);
   DECODE_CLEAR_SEGOVR(m);
    }

/*opcode=0xa9*/
static void i86op_test_AX_IMM(PC_ENV *m)
{
   int  imm;
   imm = fetch_word_imm(m);
   test_word(m, m->R_AX, imm);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xaa*/
static void i86op_stos_byte(PC_ENV *m)
{
   int  inc;
   if (ACCESS_FLAG(m, F_DF)) /* down */
     inc = -1;
   else
     inc = 1;
   if (m->sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE))
       {
      /* dont care whether REPE or REPNE */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           store_data_byte_abs(m,m->R_ES,m->R_DI,m->R_AL);
           m->R_CX -= 1;
           m->R_DI += inc;
        }
      m->sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
       }
   else
       {
      store_data_byte_abs(m,m->R_ES,m->R_DI,m->R_AL);
      m->R_DI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xab*/
static void i86op_stos_word(PC_ENV *m)
{
   int   inc;
   if (ACCESS_FLAG(m, F_DF)) /* down */
     inc = -2;
   else
     inc = 2;
   if (m->sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE))
       {
      /* dont care whether REPE or REPNE */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           store_data_word_abs(m,m->R_ES,m->R_DI,m->R_AX);
           m->R_CX -= 1;
           m->R_DI += inc;
        }
      m->sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
       }
   else
       {
      store_data_word_abs(m,m->R_ES,m->R_DI,m->R_AX);
      m->R_DI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xac*/
static void i86op_lods_byte(PC_ENV *m)
{
   int  inc;
   if (ACCESS_FLAG(m,F_DF)) /* down */
     inc = -1;
   else
     inc = 1;
   if (m->sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE))
       {
      /* dont care whether REPE or REPNE */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           m->R_AL = fetch_data_byte(m,m->R_SI);
           m->R_CX -= 1;
           m->R_SI += inc;
        }
      m->sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
       }
   else
       {
      m->R_AL = fetch_data_byte(m,m->R_SI);
      m->R_SI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xad*/
static void i86op_lods_word(PC_ENV *m)
{
   int   inc;
   if (ACCESS_FLAG(m,F_DF)) /* down */
     inc = -2;
   else
     inc = 2;
   if (m->sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE))
       {
      /* dont care whether REPE or REPNE */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           m->R_AX = fetch_data_word(m,m->R_SI);
           m->R_CX -= 1;
           m->R_SI += inc;
        }
      m->sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
       }
   else
       {
      m->R_AX = fetch_data_word(m,m->R_SI);
      m->R_SI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xae*/
static void i86op_scas_byte(PC_ENV *m)
{
   int8 val2;
   int  inc;
   if (ACCESS_FLAG(m,F_DF)) /* down */
     inc = -1;
   else
     inc = 1;
   if (m->sysmode & SYSMODE_PREFIX_REPE)
       {
      /* REPE  */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val2 = fetch_data_byte_abs(m,m->R_ES,m->R_DI);
           cmp_byte(m, m->R_AL,val2);
           m->R_CX -= 1;
           m->R_DI += inc;
           if (ACCESS_FLAG(m,F_ZF)==0)
               break;
        }
      m->sysmode &= ~SYSMODE_PREFIX_REPE;
       }
   else if (m->sysmode & SYSMODE_PREFIX_REPNE)
       {
      /* REPNE  */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val2 = fetch_data_byte_abs(m,m->R_ES,m->R_DI);
           cmp_byte(m, m->R_AL,val2);
           m->R_CX -= 1;
           m->R_DI += inc;
           if (ACCESS_FLAG(m,F_ZF))
               break;  /* zero flag set means equal */
        }
      m->sysmode &= ~SYSMODE_PREFIX_REPNE;
       }
   else
       {
      val2 = fetch_data_byte_abs(m,m->R_ES,m->R_DI);
      cmp_byte(m, m->R_AL,val2);
      m->R_DI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xaf*/
static void i86op_scas_word(PC_ENV *m)
{
   int16 val2;
   int  inc;
   if (ACCESS_FLAG(m, F_DF)) /* down */
     inc = -2;
   else
     inc = 2;
   if (m->sysmode & SYSMODE_PREFIX_REPE)
       {
      /* REPE  */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val2 = fetch_data_word_abs(m,m->R_ES,m->R_DI);
           cmp_word(m,m->R_AX,val2);
           m->R_CX -= 1;
           m->R_DI += inc;
           if (ACCESS_FLAG(m,F_ZF)==0)
               break;
        }
      m->sysmode &= ~SYSMODE_PREFIX_REPE;
       }
   else if (m->sysmode & SYSMODE_PREFIX_REPNE)
       {
      /* REPNE  */
      /* move them until CX is ZERO. */
      while (m->R_CX != 0)
        {
           val2 = fetch_data_word_abs(m,m->R_ES,m->R_DI);
           cmp_word(m, m->R_AX,val2);
           m->R_CX -= 1;
           m->R_DI += inc;
           if (ACCESS_FLAG(m,F_ZF))
               break;  /* zero flag set means equal */
        }
      m->sysmode &= ~SYSMODE_PREFIX_REPNE;
       }
   else
       {
      val2 = fetch_data_word_abs(m,m->R_ES,m->R_DI);
      cmp_word(m, m->R_AX,val2);
      m->R_DI += inc;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb0*/
static void i86op_mov_byte_AL_IMM(PC_ENV *m)
{
   uint8 imm;
   imm  =  fetch_byte_imm(m);
   m->R_AL = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb1*/
static void i86op_mov_byte_CL_IMM(PC_ENV *m)
{
   uint8 imm;
   imm  =  fetch_byte_imm(m);
   m->R_CL = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb2*/
static void i86op_mov_byte_DL_IMM(PC_ENV *m)
{
   uint8 imm;
   imm  =  fetch_byte_imm(m);
   m->R_DL = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb3*/
static void i86op_mov_byte_BL_IMM(PC_ENV *m)
{
   uint8 imm;
   imm  =  fetch_byte_imm(m);
   m->R_BL = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb4*/
static void i86op_mov_byte_AH_IMM(PC_ENV *m)
{
   uint8 imm;
   imm  =  fetch_byte_imm(m);
   m->R_AH = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb5*/
static void i86op_mov_byte_CH_IMM(PC_ENV *m)
{
   uint8 imm;
   imm  =  fetch_byte_imm(m);
   m->R_CH = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb6*/
static void i86op_mov_byte_DH_IMM(PC_ENV *m)
{
   uint8 imm;
   imm  =  fetch_byte_imm(m);
   m->R_DH = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb7*/
static void i86op_mov_byte_BH_IMM(PC_ENV *m)
{
   uint8 imm;
   imm  =  fetch_byte_imm(m);
   m->R_BH = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb8*/
static void i86op_mov_word_AX_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_AX = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xb9*/
static void i86op_mov_word_CX_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_CX = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xba*/
static void i86op_mov_word_DX_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_DX = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xbb*/
static void i86op_mov_word_BX_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_BX = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xbc*/
static void i86op_mov_word_SP_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_SP = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xbd*/
static void i86op_mov_word_BP_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_BP = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xbe*/
static void i86op_mov_word_SI_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_SI = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xbf*/
static void i86op_mov_word_DI_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_DI = imm;
   DECODE_CLEAR_SEGOVR(m);
}

/* c0 === ILLEGAL OPERAND */
/* c1 === ILLEGAL OPERAND */

/*opcode=0xc2*/
static void i86op_ret_near_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_IP = pop_word(m);
   m->R_SP += imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xc3*/
static void i86op_ret_near(PC_ENV *m)
{
   m->R_IP = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xc4*/
static void i86op_les_R_IMM(PC_ENV *m)
{
   uint16      mod,rh,rl;
   uint16      *dstreg;
   uint16      srcoffset;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      dstreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      *dstreg = fetch_data_word(m,srcoffset);
      m->R_ES = fetch_data_word(m,srcoffset+2);
      break;
    case 1:
      dstreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      *dstreg = fetch_data_word(m,srcoffset);
      m->R_ES = fetch_data_word(m,srcoffset+2);
      break;
    case 2:
      dstreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      *dstreg = fetch_data_word(m,srcoffset);
      m->R_ES = fetch_data_word(m,srcoffset+2);
      break;
    case 3:   /* register to register */
      /* UNDEFINED! */
      ;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xc5*/
static void i86op_lds_R_IMM(PC_ENV *m)
{
   uint16      mod,rh,rl;
   uint16      *dstreg;
   uint16      srcoffset;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      dstreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm00_address(m,rl);
      *dstreg = fetch_data_word(m,srcoffset);
      m->R_DS = fetch_data_word(m,srcoffset+2);
      break;
    case 1:
      dstreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm01_address(m,rl);
      *dstreg = fetch_data_word(m,srcoffset);
      m->R_DS = fetch_data_word(m,srcoffset+2);
      break;
    case 2:
      dstreg  =  DECODE_RM_WORD_REGISTER(m,rh);
      srcoffset=decode_rm10_address(m,rl);
      *dstreg = fetch_data_word(m,srcoffset);
      m->R_DS = fetch_data_word(m,srcoffset+2);
      break;
    case 3:   /* register to register */
      /* UNDEFINED! */
      ;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xc6*/
static void i86op_mov_byte_RM_IMM(PC_ENV *m)
{
   uint16       mod,rl,rh;
   uint8         *destreg;
   uint16           destoffset;
   uint8           imm;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   if (rh != 0)
       {
      halt_sys(m);
       }
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      imm = fetch_byte_imm(m);
      store_data_byte(m,destoffset,imm);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      imm = fetch_byte_imm(m);
      store_data_byte(m,destoffset,imm);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      imm = fetch_byte_imm(m);
      store_data_byte(m,destoffset,imm);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      imm = fetch_byte_imm(m);
      *destreg =  imm;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xc7*/
static void i86op_mov_word_RM_IMM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg;
   uint16      destoffset;
   uint16      imm;
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   if (rh != 0)
       {
      halt_sys(m);
       }
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      imm = fetch_word_imm(m);
      store_data_word(m,destoffset,imm);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      imm = fetch_word_imm(m);
      store_data_word(m,destoffset,imm);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      imm = fetch_word_imm(m);
      store_data_word(m,destoffset,imm);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      imm = fetch_word_imm(m);
      *destreg = imm;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
    }

/*opcode=0xc8  ILLEGAL OP*/
/*opcode=0xc9  ILLEGAL OP*/

/*opcode=0xca*/
static void i86op_ret_far_IMM(PC_ENV *m)
{
   uint16 imm;
   imm  =  fetch_word_imm(m);
   m->R_IP = pop_word(m);
   m->R_CS = pop_word(m);
   m->R_SP += imm;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xcb*/
static void i86op_ret_far(PC_ENV *m)
{
   m->R_IP = pop_word(m);
   m->R_CS = pop_word(m);
   DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xcc*/
static void i86op_int3(PC_ENV *m)
{
    uint16 tmp;
    /* access the segment register */
    {
       tmp = m->R_FLG;
       push_word(m, tmp);
       CLEAR_FLAG(m, F_IF);
       CLEAR_FLAG(m, F_TF);
/* [JCE] If we're interrupting between a segment override (or REP override)
 * and the following instruction, decrease IP to get back to the prefix */
       if (m->sysmode &    (SYSMODE_SEGMASK     |
                SYSMODE_PREFIX_REPE |
                SYSMODE_PREFIX_REPNE))
       {
          --m->R_IP;
       }
       push_word(m, m->R_CS);
       push_word(m, m->R_IP);
/* [JCE] CS and IP were the wrong way round... */
       tmp = mem_access_word(m, 3 * 4);
       m->R_IP = tmp;
       tmp = mem_access_word(m, 3 * 4 + 2);
       m->R_CS = tmp;
    }
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xcd*/
static void i86op_int_IMM(PC_ENV *m)
{
    uint16 tmp;
    uint8 intnum;
    intnum = fetch_byte_imm(m);
    {
       tmp = m->R_FLG;
       push_word(m, tmp);
       CLEAR_FLAG(m, F_IF);
       CLEAR_FLAG(m, F_TF);
/* [JCE] If we're interrupting between a segment override (or REP override)
 * and the following instruction, decrease IP to get back to the prefix */
       if (m->sysmode &    (SYSMODE_SEGMASK     |
                SYSMODE_PREFIX_REPE |
                SYSMODE_PREFIX_REPNE))
       {
          --m->R_IP;
       }
       push_word(m, m->R_CS);
       push_word(m, m->R_IP);
/* [JCE] CS and IP were the wrong way round... */
       tmp = mem_access_word(m, intnum * 4);
       m->R_IP = tmp;
       tmp = mem_access_word(m, intnum * 4 + 2);
       m->R_CS = tmp;
    }
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xce*/
static void i86op_into(PC_ENV *m)
{
    uint16 tmp;
    if (ACCESS_FLAG(m,F_OF))
    {
           {
          tmp = m->R_FLG;
          push_word(m, tmp);
          CLEAR_FLAG(m, F_IF);
          CLEAR_FLAG(m, F_TF);
/* [JCE] If we're interrupting between a segment override (or REP override)
 * and the following instruction, decrease IP to get back to the prefix */
          if (m->sysmode &    (SYSMODE_SEGMASK     |
                    SYSMODE_PREFIX_REPE |
                    SYSMODE_PREFIX_REPNE))
          {
              --m->R_IP;
          }
          push_word(m, m->R_CS);
          push_word(m, m->R_IP);
/* [JCE] CS and IP were the wrong way round... */
          tmp = mem_access_word(m, 4 * 4);
          m->R_IP = tmp;
          tmp = mem_access_word(m, 4 * 4 + 2);
          m->R_CS = tmp;
           }
    }
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xcf*/
static void i86op_iret(PC_ENV *m)
{
    m->R_IP = pop_word(m);
    m->R_CS = pop_word(m);
    m->R_FLG = pop_word(m);
    DECODE_CLEAR_SEGOVR(m);
}

static uint8 (*opcD0_byte_operation[])(PC_ENV *m,uint8 d, uint8 s) =
      /* used by opcodes d0 and d2. */
{
    rol_byte,
    ror_byte,
    rcl_byte,
    rcr_byte,
    shl_byte,
    shr_byte,
    shl_byte,   /* sal_byte === shl_byte  by definition */
    sar_byte,
};

/* opcode=0xd0*/
static void i86op_opcD0_byte_RM_1(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg;
   uint16      destoffset;
   uint8       destval;
   /* Yet another weirdo special case instruction format.  Part of the
      opcode held below in "RH".  Doubly nested case would
      result, except that the decoded instruction
    */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   /* know operation, decode the mod byte to find the addressing
      mode. */
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      destval = (*opcD0_byte_operation[rh])(m, destval,1);
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      destval = (*opcD0_byte_operation[rh])(m, destval, 1);
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      destval = (*opcD0_byte_operation[rh])(m, destval, 1);
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      destval = (*opcD0_byte_operation[rh])(m, *destreg, 1);
      *destreg = destval;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
     }

static uint16 (*opcD1_word_operation[])(PC_ENV *m,uint16 s,uint16 d) =
      /* used by opcodes d1 and d3. */
{    rol_word,
    ror_word,
    rcl_word,
    rcr_word,
    shl_word,
    shr_word,
    shl_word,   /* sal_byte === shl_byte  by definition */
    sar_word,
};

/* opcode=0xd1*/
static void i86op_opcD1_word_RM_1(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg;
   uint16      destoffset;
   uint16      destval;
   /* Yet another weirdo special case instruction format.  Part of the
      opcode held below in "RH".  Doubly nested case would
      result, except that the decoded instruction
    */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   /* know operation, decode the mod byte to find the addressing
      mode. */
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      destval = (*opcD1_word_operation[rh])(m, destval,1);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      destval = (*opcD1_word_operation[rh])(m, destval, 1);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      destval = (*opcD1_word_operation[rh])(m, destval, 1);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      destval = (*opcD1_word_operation[rh])(m, *destreg, 1);
      *destreg = destval;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xd2*/
static void i86op_opcD2_byte_RM_CL(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg;
   uint16      destoffset;
   uint8       destval;
   uint8       amt;
   /* Yet another weirdo special case instruction format.  Part of the
      opcode held below in "RH".  Doubly nested case would
      result, except that the decoded instruction
    */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   amt = m->R_CL;
   /* know operation, decode the mod byte to find the addressing
      mode. */
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      destval = (*opcD0_byte_operation[rh])(m, destval,amt);
      store_data_byte(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      destval = (*opcD0_byte_operation[rh])(m, destval, amt);
      store_data_byte(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_byte(m,destoffset);
      destval = (*opcD0_byte_operation[rh])(m, destval, amt);
      store_data_byte(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      destval = (*opcD0_byte_operation[rh])(m, *destreg, amt);
      *destreg = destval;
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xd3*/
static void i86op_opcD3_word_RM_CL(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint16      *destreg;
   uint16      destoffset;
   uint16      destval;
   uint8       amt;
   /* Yet another weirdo special case instruction format.  Part of the
      opcode held below in "RH".  Doubly nested case would
      result, except that the decoded instruction
    */
   FETCH_DECODE_MODRM(m,mod,rh,rl);
   amt = m->R_CL;
   /* know operation, decode the mod byte to find the addressing
      mode. */
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      destval = (*opcD1_word_operation[rh])(m, destval, amt);
      store_data_word(m,destoffset,destval);
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      destval = (*opcD1_word_operation[rh])(m, destval, amt);
      store_data_word(m,destoffset,destval);
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      destval = fetch_data_word(m,destoffset);
      destval = (*opcD1_word_operation[rh])(m, destval, amt);
      store_data_word(m,destoffset,destval);
      break;
    case 3:   /* register to register */
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      *destreg = (*opcD1_word_operation[rh])(m, *destreg, amt);
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

static void sys_fatal(int error, const char *fmt, ...)
{
  va_list   p;
  va_start(p, fmt);
  fprintf(stderr, "Fatal error: ");
  if (error != 0)
      {
    fprintf(stderr, "<%d>",error);
    fprintf(stderr,"%s",strerror(error));
      }
  vfprintf(stderr, fmt, p);
  va_end(p);
  fprintf(stderr, NLP "Exiting..." NLP);
  exit(1);
}

/* opcode=0xd4*/
static void i86op_aam(PC_ENV *m)
{   uint8 a;
    a = fetch_byte_imm(m);  /* this is a stupid encoding. */
    if (a != 10)
        sys_fatal(0,"error decoding aam" NLP);
    /* note the type change here --- returning AL and AH in AX. */
    m->R_AX = aam_word(m,m->R_AL);
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xd5*/
static void i86op_aad(PC_ENV *m)
{
    m->R_AX = aad_word(m,m->R_AX);
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xd6 ILLEGAL OPCODE */

/* opcode=0xd7 */
static void i86op_xlat(PC_ENV *m)
{
   uint16 addr;
   addr = m->R_BX + (uint8)m->R_AL;
   m->R_AL = fetch_data_byte(m,addr);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe0*/
static void i86op_loopne(PC_ENV *m)
{
   int16 ip;
   ip = (int8)fetch_byte_imm(m);
   ip += (int16)m->R_IP;
   m->R_CX -= 1;
   if (m->R_CX != 0 && !ACCESS_FLAG(m,F_ZF))  /* CX != 0 and !ZF */
     m->R_IP = ip;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe1*/
static void i86op_loope(PC_ENV *m)
{
   int16 ip;
   ip = (int8)fetch_byte_imm(m);
   ip += (int16)m->R_IP;
   m->R_CX -= 1;
   if (m->R_CX != 0 && ACCESS_FLAG(m,F_ZF))  /* CX != 0 and ZF */
     m->R_IP = ip;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe2*/
static void i86op_loop(PC_ENV *m)
{
   int16 ip;
   ip = (int8)fetch_byte_imm(m);
   ip += (int16)m->R_IP;
   m->R_CX -= 1;
   if (m->R_CX != 0)
     m->R_IP = ip;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe3*/
static void i86op_jcxz(PC_ENV *m)
{
   int16 offset,target;
   /* jump to byte offset if overflow flag is set */
   offset = (int8)fetch_byte_imm(m);   /* sign extended ??? */
   target = (int16)m->R_IP + offset;
   if (m->R_CX == 0)
     m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe4*/
static void i86op_in_byte_AL_IMM(PC_ENV *m)
{
   uint8 port;
   port = (uint8)fetch_byte_imm(m);
   m->R_AL = in(port);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe5*/
static void i86op_in_word_AX_IMM(PC_ENV *m)
{
   uint8 port;
   port = (uint8)fetch_byte_imm(m);
   m->R_AX = in(port);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe6*/
static void i86op_out_byte_IMM_AL(PC_ENV *m)
{
   uint8 port;
   port = (uint8)fetch_byte_imm(m);
   out(port, m->R_AL);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe7*/
static void i86op_out_word_IMM_AX(PC_ENV *m)
{
   uint8 port;
   port = (uint8)fetch_byte_imm(m);
   out(port, m->R_AX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe8*/
static void i86op_call_near_IMM(PC_ENV *m)
{
   int16 ip;
   /* weird.  Thought this was a signed disp! */
   ip = (int16)fetch_word_imm(m);
   ip += (int16)m->R_IP;                  /* CHECK SIGN */
   push_word(m,m->R_IP);
   m->R_IP = ip;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xe9*/
static void i86op_jump_near_IMM(PC_ENV *m)
{
   int ip;
   /* weird.  Thought this was a signed disp too! */
   ip = (int16)fetch_word_imm(m);
   ip += (int16)m->R_IP;              /* CHECK SIGN */
   m->R_IP = ip;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xea*/
static void i86op_jump_far_IMM(PC_ENV *m)
{
   uint16 cs,ip;
   ip = fetch_word_imm(m);
   cs = fetch_word_imm(m);
   m->R_IP = ip;
   m->R_CS = cs;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xeb*/
static void i86op_jump_byte_IMM(PC_ENV *m)
{
   int8 offset;
   uint16 target;
   offset = (int8) fetch_byte_imm(m);             /* CHECK */
/*   sim_printf("jump byte imm offset=%d\n",offset);*/
   target = (int16) m->R_IP + offset;
   m->R_IP = target;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xec*/
static void i86op_in_byte_AL_DX(PC_ENV *m)
{
   m->R_AL = in(m->R_DX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xed*/
static void i86op_in_word_AX_DX(PC_ENV *m)
{
   m->R_AX = in(m->R_DX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xee*/
static void i86op_out_byte_DX_AL(PC_ENV *m)
{
   out(m->R_DX, m->R_AL);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xef*/
static void i86op_out_word_DX_AX(PC_ENV *m)
{
   out(m->R_DX, m->R_AX);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xf0*/
static void i86op_lock(PC_ENV *m)
{
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xf1 ILLEGAL OPERATION*/

/*opcode=0xf2*/
static void i86op_repne(PC_ENV *m)
{
   m->sysmode |= SYSMODE_PREFIX_REPNE;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xf3*/
static void i86op_repe(PC_ENV *m)
{
   m->sysmode |= SYSMODE_PREFIX_REPE;
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xf4*/
static void i86op_halt(PC_ENV *m)
{
   halt_sys(m);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xf5*/
static void i86op_cmc(PC_ENV *m)
{
   /* complement the carry flag. */
   TOGGLE_FLAG(m,F_CF);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xf6*/
static void i86op_opcF6_byte_RM(PC_ENV *m)
{
   uint16      mod,rl,rh;
   uint8       *destreg;
   uint16      destoffset;
   uint8       destval,srcval;
   /* long, drawn out code follows.  Double switch for a total
      of 32 cases.  */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:   /* mod=00 */
      switch(rh)
          {
           case 0:  /* test byte imm */
         destoffset=decode_rm00_address(m,rl);
         srcval  =  fetch_byte_imm(m);
         destval = fetch_data_byte(m,destoffset);
         test_byte(m, destval, srcval);
         break;
           case 1:
         halt_sys(m);
         break;
           case 2:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         destval = not_byte(m, destval);
         store_data_byte(m,destoffset,destval);
         break;
           case 3:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         destval = neg_byte(m, destval);
         store_data_byte(m,destoffset,destval);
         break;
           case 4:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         mul_byte(m, destval);
         break;
           case 5:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         imul_byte(m, destval);
         break;
           case 6:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         div_byte(m, destval);
         break;
           case 7:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         idiv_byte(m, destval);
         break;
          }
      break;  /* end mod==00 */
    case 1:   /* mod=01 */
      switch(rh)
          {
           case 0:  /* test byte imm */
         destoffset=decode_rm01_address(m,rl);
         srcval  =  fetch_byte_imm(m);
         destval = fetch_data_byte(m,destoffset);
         test_byte(m, destval, srcval);
         break;
           case 1:
         halt_sys(m);
         break;
           case 2:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         destval = not_byte(m, destval);
         store_data_byte(m,destoffset,destval);
         break;
           case 3:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         destval = neg_byte(m, destval);
         store_data_byte(m,destoffset,destval);
         break;
           case 4:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         mul_byte(m, destval);
         break;
           case 5:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         imul_byte(m, destval);
         break;
           case 6:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         div_byte(m, destval);
         break;
           case 7:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         idiv_byte(m, destval);
         break;
          }
      break;  /* end mod==01 */
    case 2:   /* mod=10 */
      switch(rh)
          {
           case 0:  /* test byte imm */
         destoffset=decode_rm10_address(m,rl);
         srcval  =  fetch_byte_imm(m);
         destval = fetch_data_byte(m,destoffset);
         test_byte(m, destval, srcval);
         break;
           case 1:
         halt_sys(m);
         break;
           case 2:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         destval = not_byte(m, destval);
         store_data_byte(m,destoffset,destval);
         break;
           case 3:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         destval = neg_byte(m, destval);
         store_data_byte(m,destoffset,destval);
         break;
           case 4:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         mul_byte(m, destval);
         break;
           case 5:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         imul_byte(m, destval);
         break;
           case 6:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         div_byte(m, destval);
         break;
           case 7:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_byte(m,destoffset);
         idiv_byte(m, destval);
         break;
          }
      break;  /* end mod==10 */
    case 3:   /* mod=11 */
      switch(rh)
          {
           case 0:  /* test byte imm */
         destreg=DECODE_RM_BYTE_REGISTER(m,rl);
         srcval  =  fetch_byte_imm(m);
         test_byte(m, *destreg, srcval);
         break;
           case 1:
         halt_sys(m);
         break;
           case 2:
         destreg=DECODE_RM_BYTE_REGISTER(m,rl);
         *destreg = not_byte(m, *destreg);
         break;
           case 3:
         destreg=DECODE_RM_BYTE_REGISTER(m,rl);
         *destreg = neg_byte(m, *destreg);
         break;
           case 4:
         destreg=DECODE_RM_BYTE_REGISTER(m,rl);
         mul_byte(m, *destreg);  /*!!!  */
         break;
           case 5:
         destreg=DECODE_RM_BYTE_REGISTER(m,rl);
         imul_byte(m, *destreg);
         break;
           case 6:
         destreg=DECODE_RM_BYTE_REGISTER(m,rl);
         div_byte(m, *destreg);
         break;
           case 7:
         destreg=DECODE_RM_BYTE_REGISTER(m,rl);
         idiv_byte(m, *destreg);
         break;
          }
      break;  /* end mod==11 */
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xf7*/
static void i86op_opcF7_word_RM(PC_ENV *m)
{
   uint16   mod,rl,rh;
   uint16   *destreg;
   uint16   destoffset;
   uint16   destval,srcval;
   /* long, drawn out code follows.  Double switch for a total
      of 32 cases.  */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:   /* mod=00 */
      switch(rh)
          {
           case 0:  /* test word imm */
         destoffset=decode_rm00_address(m,rl);
         srcval  =  fetch_word_imm(m);
         destval = fetch_data_word(m,destoffset);
         test_word(m, destval, srcval);
         break;
           case 1:
         halt_sys(m);
         break;
           case 2:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         destval = not_word(m, destval);
         store_data_word(m,destoffset,destval);
         break;
           case 3:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         destval = neg_word(m, destval);
         store_data_word(m,destoffset,destval);
         break;
           case 4:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         mul_word(m, destval);
         break;
           case 5:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         imul_word(m, destval);
         break;
           case 6:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         div_word(m, destval);
         break;
           case 7:
         destoffset=decode_rm00_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         idiv_word(m, destval);
         break;
          }
      break;  /* end mod==00 */
    case 1:   /* mod=01 */
      switch(rh)
          {
           case 0:  /* test word imm */
         destoffset=decode_rm01_address(m,rl);
         srcval  =  fetch_word_imm(m);
         destval = fetch_data_word(m,destoffset);
         test_word(m, destval, srcval);
         break;
           case 1:
         halt_sys(m);
         break;
           case 2:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         destval = not_word(m, destval);
         store_data_word(m,destoffset,destval);
         break;
           case 3:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         destval = neg_word(m, destval);
         store_data_word(m,destoffset,destval);
         break;
           case 4:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         mul_word(m, destval);
         break;
           case 5:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         imul_word(m, destval);
         break;
           case 6:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         div_word(m, destval);
         break;
           case 7:
         destoffset=decode_rm01_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         idiv_word(m, destval);
         break;
          }
      break;  /* end mod==01 */
    case 2:   /* mod=10 */
      switch(rh)
          {
           case 0:  /* test word imm */
         destoffset=decode_rm10_address(m,rl);
         srcval  =  fetch_word_imm(m);
         destval = fetch_data_word(m,destoffset);
         test_word(m, destval, srcval);
         break;
           case 1:
         halt_sys(m);
         break;
           case 2:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         destval = not_word(m, destval);
         store_data_word(m,destoffset,destval);
         break;
           case 3:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         destval = neg_word(m, destval);
         store_data_word(m,destoffset,destval);
         break;
           case 4:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         mul_word(m, destval);
         break;
           case 5:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         imul_word(m, destval);
         break;
           case 6:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         div_word(m, destval);
         break;
           case 7:
         destoffset=decode_rm10_address(m,rl);
         destval = fetch_data_word(m,destoffset);
         idiv_word(m, destval);
         break;
          }
      break;  /* end mod==10 */
    case 3:   /* mod=11 */
      switch(rh)
          {
           case 0:  /* test word imm */
         destreg=DECODE_RM_WORD_REGISTER(m,rl);
         srcval  =  fetch_word_imm(m);
         test_word(m, *destreg, srcval);
         break;
           case 1:
         halt_sys(m);
         break;
           case 2:
         destreg=DECODE_RM_WORD_REGISTER(m,rl);
         *destreg = not_word(m, *destreg);
         break;
           case 3:
         destreg=DECODE_RM_WORD_REGISTER(m,rl);
         *destreg = neg_word(m, *destreg);
         break;
           case 4:
         destreg=DECODE_RM_WORD_REGISTER(m,rl);
         mul_word(m, *destreg);  /*!!!  */
         break;
           case 5:
         destreg=DECODE_RM_WORD_REGISTER(m,rl);
         imul_word(m, *destreg);
         break;
           case 6:
         destreg=DECODE_RM_WORD_REGISTER(m,rl);
         div_word(m, *destreg);
         break;
           case 7:
         destreg=DECODE_RM_WORD_REGISTER(m,rl);
         idiv_word(m, *destreg);
         break;
          }
      break;  /* end mod==11 */
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xf8*/
static void i86op_clc(PC_ENV *m)
{
   /* clear the carry flag. */
   CLEAR_FLAG(m, F_CF);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xf9*/
static void i86op_stc(PC_ENV *m)
{
   /* set the carry flag. */
   SET_FLAG(m, F_CF);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xfa*/
static void i86op_cli(PC_ENV *m)
{
   /* clear interrupts. */
   CLEAR_FLAG(m, F_IF);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xfb*/
static void i86op_sti(PC_ENV *m)
{
   /* enable  interrupts. */
   SET_FLAG(m, F_IF);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xfc*/
static void i86op_cld(PC_ENV *m)
{
   /* clear interrupts. */
   CLEAR_FLAG(m, F_DF);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xfd*/
static void i86op_std(PC_ENV *m)
{
   /* clear interrupts. */
   SET_FLAG(m, F_DF);
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xfe*/
static void i86op_opcFE_byte_RM(PC_ENV *m)
{
   /* Yet another damned special case instruction. */
   uint16      mod,rh,rl;
   uint8       destval;
   uint16      destoffset;
   uint8       *destreg;
   /* ARRGH, ANOTHER GODDAMN SPECIAL CASE INSTRUCTION!!! */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      switch (rh)
        {
         case 0:  /* inc word ptr ... */
           destval = fetch_data_byte(m,destoffset);
           destval = inc_byte(m,destval);
           store_data_byte(m,destoffset,destval);
           break;
         case 1:  /* dec word ptr ... */
           destval = fetch_data_byte(m,destoffset);
           destval = dec_byte(m,destval);
           store_data_byte(m,destoffset,destval);
           break;
        }
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      switch (rh)
          {
           case 0:
         destval = fetch_data_byte(m,destoffset);
         destval = inc_byte(m,destval);
         store_data_byte(m,destoffset,destval);
         break;
           case 1:
         destval = fetch_data_byte(m,destoffset);
         destval = dec_byte(m,destval);
         store_data_byte(m,destoffset,destval);
         break;
          }
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      switch (rh)
          {
           case 0:
         destval = fetch_data_byte(m,destoffset);
         destval = inc_byte(m,destval);
         store_data_byte(m,destoffset,destval);
         break;
           case 1:
         destval = fetch_data_byte(m,destoffset);
         destval = dec_byte(m,destval);
         store_data_byte(m,destoffset,destval);
         break;
          }
      break;
    case 3:
      destreg  = DECODE_RM_BYTE_REGISTER(m,rl);
      switch (rh)
          {
           case 0:
         *destreg = inc_byte(m,*destreg);
         break;
           case 1:
         *destreg = dec_byte(m,*destreg);
         break;
          }
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/*opcode=0xff*/
static void i86op_opcFF_word_RM(PC_ENV *m)
{
   uint16      mod,rh,rl;
   uint16      destval,destval2;
   uint16      destoffset;
   uint16     *destreg;
   /* ANOTHER DAMN SPECIAL CASE INSTRUCTION!!! */
      FETCH_DECODE_MODRM(m,mod,rh,rl);
   switch (mod)
       {
    case 0:
      destoffset=decode_rm00_address(m,rl);
      switch (rh)
          {
           case 0:  /* inc word ptr ... */
         destval = fetch_data_word(m,destoffset);
         destval = inc_word(m,destval);
         store_data_word(m,destoffset,destval);
         break;
           case 1:  /* dec word ptr ... */
         destval = fetch_data_word(m,destoffset);
         destval = dec_word(m,destval);
         store_data_word(m,destoffset,destval);
         break;
           case 2:  /* call word ptr ... */
         destval = fetch_data_word(m,destoffset);
         push_word(m,m->R_IP);
         m->R_IP = destval;
         break;
           case 3:  /* call far ptr ... */
         destval = fetch_data_word(m,destoffset);
         destval2 = fetch_data_word(m,destoffset+2);
         push_word(m,m->R_CS);
         m->R_CS = destval2;
         push_word(m,m->R_IP);
         m->R_IP = destval;
         break;
           case 4:  /* jmp word ptr ... */
         destval = fetch_data_word(m,destoffset);
         m->R_IP = destval;
         break;
           case 5:  /* jmp far ptr ... */
         destval = fetch_data_word(m,destoffset);
         destval2 = fetch_data_word(m,destoffset+2);
         m->R_IP = destval;
         m->R_CS = destval2;
         break;
           case 6:  /*  push word ptr ... */
         destval = fetch_data_word(m,destoffset);
         push_word(m,destval);
         break;
          }
      break;
    case 1:
      destoffset=decode_rm01_address(m,rl);
      switch (rh)
          {
           case 0:
         destval = fetch_data_word(m,destoffset);
         destval = inc_word(m,destval);
         store_data_word(m,destoffset,destval);
         break;
           case 1:
         destval = fetch_data_word(m,destoffset);
         destval = dec_word(m,destval);
         store_data_word(m,destoffset,destval);
         break;
           case 2:  /* call word ptr ... */
         destval = fetch_data_word(m,destoffset);
         push_word(m,m->R_IP);
         m->R_IP = destval;
         break;
           case 3:  /* call far ptr ... */
         destval = fetch_data_word(m,destoffset);
         destval2 = fetch_data_word(m,destoffset+2);
         push_word(m,m->R_CS);
         m->R_CS = destval2;
         push_word(m,m->R_IP);
         m->R_IP = destval;
         break;
           case 4:  /* jmp word ptr ... */
         destval = fetch_data_word(m,destoffset);
         m->R_IP = destval;
         break;
           case 5:  /* jmp far ptr ... */
         destval = fetch_data_word(m,destoffset);
         destval2 = fetch_data_word(m,destoffset+2);
         m->R_IP = destval;
         m->R_CS = destval2;
         break;
           case 6:  /*  push word ptr ... */
         destval = fetch_data_word(m,destoffset);
         push_word(m,destval);
         break;
          }
      break;
    case 2:
      destoffset=decode_rm10_address(m,rl);
      switch (rh)
          {
           case 0:
         destval = fetch_data_word(m,destoffset);
         destval = inc_word(m,destval);
         store_data_word(m,destoffset,destval);
         break;
           case 1:
         destval = fetch_data_word(m,destoffset);
         destval = dec_word(m,destval);
         store_data_word(m,destoffset,destval);
         break;
           case 2:  /* call word ptr ... */
         destval = fetch_data_word(m,destoffset);
         push_word(m,m->R_IP);
         m->R_IP = destval;
         break;
           case 3:  /* call far ptr ... */
         destval = fetch_data_word(m,destoffset);
         destval2 = fetch_data_word(m,destoffset+2);
         push_word(m,m->R_CS);
         m->R_CS = destval2;
         push_word(m,m->R_IP);
         m->R_IP = destval;
         break;
           case 4:  /* jmp word ptr ... */
         destval = fetch_data_word(m,destoffset);
         m->R_IP = destval;
         break;
           case 5:  /* jmp far ptr ... */
         destval = fetch_data_word(m,destoffset);
         destval2 = fetch_data_word(m,destoffset+2);
         m->R_IP = destval;
         m->R_CS = destval2;
         break;
           case 6:  /*  push word ptr ... */
         destval = fetch_data_word(m,destoffset);
         push_word(m,destval);
         break;
          }
      break;
    case 3:
      destreg  = DECODE_RM_WORD_REGISTER(m,rl);
      switch (rh)
          {
           case 0:
         *destreg = inc_word(m,*destreg);
         break;
           case 1:
         *destreg = dec_word(m,*destreg);
         break;
           case 2:  /* call word ptr ... */
         push_word(m,m->R_IP);
         m->R_IP = *destreg;
         break;
           case 3:  /* jmp far ptr ... */
         halt_sys(m);
         break;
           case 4:  /* jmp  ... */
         m->R_IP = (uint16)(*destreg);
         break;
           case 5:  /* jmp far ptr ... */
         halt_sys(m);
         break;
           case 6:
         push_word(m,*destreg);
         break;
          }
      break;
       }
   DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xd8*/
static void i86op_esc_coprocess_d8(PC_ENV *m)
{
   DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xd9*/
static void i86op_esc_coprocess_d9(PC_ENV *m)
{
    uint16 mod,rl,rh;
    FETCH_DECODE_MODRM(m,mod,rh,rl);
    switch (mod)
      {
      case 0:
        decode_rm00_address(m,rl);
        break;
      case 1:
        decode_rm01_address(m,rl);
        break;
      case 2:
        decode_rm10_address(m,rl);
        break;
      case 3:   /* register to register */
        break;
      }
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xda*/
static void i86op_esc_coprocess_da(PC_ENV *m)
{
    uint16 mod,rl,rh;
    FETCH_DECODE_MODRM(m,mod,rh,rl);
    switch (mod)
      {
      case 0:
        decode_rm00_address(m,rl);
        break;
      case 1:
        decode_rm01_address(m,rl);
        break;
      case 2:
        decode_rm10_address(m,rl);
        break;
      case 3:   /* register to register */
        break;
      }
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xdb*/
static void i86op_esc_coprocess_db(PC_ENV *m)
{
    uint16 mod,rl,rh;
    FETCH_DECODE_MODRM(m,mod,rh,rl);
    switch (mod)
      {
      case 0:
        decode_rm00_address(m,rl);
        break;
      case 1:
        decode_rm01_address(m,rl);
        break;
      case 2:
        decode_rm10_address(m,rl);
        break;
      case 3:   /* register to register */
        break;
      }
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xdc*/
static void i86op_esc_coprocess_dc(PC_ENV *m)
{
    uint16 mod,rl,rh;
    FETCH_DECODE_MODRM(m,mod,rh,rl);
    switch (mod)
      {
      case 0:
        decode_rm00_address(m,rl);
        break;
      case 1:
        decode_rm01_address(m,rl);
        break;
      case 2:
        decode_rm10_address(m,rl);
        break;
      case 3:   /* register to register */
        break;
      }
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xdd*/
static void i86op_esc_coprocess_dd(PC_ENV *m)
{
    uint16 mod,rl,rh;
    FETCH_DECODE_MODRM(m,mod,rh,rl);
    switch (mod)
      {
      case 0:
        decode_rm00_address(m,rl);
        break;
      case 1:
        decode_rm01_address(m,rl);
        break;
      case 2:
        decode_rm10_address(m,rl);
        break;
      case 3:   /* register to register */
        break;
      }
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xde*/
static void i86op_esc_coprocess_de(PC_ENV *m)
  {
    uint16 mod,rl,rh;
    FETCH_DECODE_MODRM(m,mod,rh,rl);
    switch (mod)
      {
      case 0:
        decode_rm00_address(m,rl);
        break;
      case 1:
        decode_rm01_address(m,rl);
        break;
      case 2:
        decode_rm10_address(m,rl);
        break;
      case 3:   /* register to register */
        break;
      }
    DECODE_CLEAR_SEGOVR(m);
}

/* opcode=0xdf*/
static void i86op_esc_coprocess_df(PC_ENV *m)
  {
    uint16 mod,rl,rh;
    FETCH_DECODE_MODRM(m,mod,rh,rl);
    switch (mod)
      {
      case 0:
        decode_rm00_address(m,rl);
        break;
      case 1:
        decode_rm01_address(m,rl);
        break;
      case 2:
        decode_rm10_address(m,rl);
        break;
      case 3:   /* register to register */
        break;
      }
    DECODE_CLEAR_SEGOVR(m);
  }

/* OPERAND TABLE                                       */

OP  i86_optab[256] =  {

/*  0x00 */  i86op_add_byte_RM_R,
/*  0x01 */  i86op_add_word_RM_R,
/*  0x02 */  i86op_add_byte_R_RM,
/*  0x03 */  i86op_add_word_R_RM,
/*  0x04 */  i86op_add_byte_AL_IMM,
/*  0x05 */  i86op_add_word_AX_IMM,
/*  0x06 */  i86op_push_ES,
/*  0x07 */  i86op_pop_ES,

/*  0x08 */  i86op_or_byte_RM_R,
/*  0x09 */  i86op_or_word_RM_R,
/*  0x0a */  i86op_or_byte_R_RM,
/*  0x0b */  i86op_or_word_R_RM,
/*  0x0c */  i86op_or_byte_AL_IMM,
/*  0x0d */  i86op_or_word_AX_IMM,
/*  0x0e */  i86op_push_CS,
/*  0x0f */  i86op_illegal_op,

/*  0x10 */  i86op_adc_byte_RM_R,
/*  0x11 */  i86op_adc_word_RM_R,
/*  0x12 */  i86op_adc_byte_R_RM,
/*  0x13 */  i86op_adc_word_R_RM,
/*  0x14 */  i86op_adc_byte_AL_IMM,
/*  0x15 */  i86op_adc_word_AX_IMM,
/*  0x16 */  i86op_push_SS,
/*  0x17 */  i86op_pop_SS,

/*  0x18 */  i86op_sbb_byte_RM_R,
/*  0x19 */  i86op_sbb_word_RM_R,
/*  0x1a */  i86op_sbb_byte_R_RM,
/*  0x1b */  i86op_sbb_word_R_RM,
/*  0x1c */  i86op_sbb_byte_AL_IMM,
/*  0x1d */  i86op_sbb_word_AX_IMM,
/*  0x1e */  i86op_push_DS,
/*  0x1f */  i86op_pop_DS,

/*  0x20 */  i86op_and_byte_RM_R,
/*  0x21 */  i86op_and_word_RM_R,
/*  0x22 */  i86op_and_byte_R_RM,
/*  0x23 */  i86op_and_word_R_RM,
/*  0x24 */  i86op_and_byte_AL_IMM,
/*  0x25 */  i86op_and_word_AX_IMM,
/*  0x26 */  i86op_segovr_ES,
/*  0x27 */  i86op_daa,

/*  0x28 */  i86op_sub_byte_RM_R,
/*  0x29 */  i86op_sub_word_RM_R,
/*  0x2a */  i86op_sub_byte_R_RM,
/*  0x2b */  i86op_sub_word_R_RM,
/*  0x2c */  i86op_sub_byte_AL_IMM,
/*  0x2d */  i86op_sub_word_AX_IMM,
/*  0x2e */  i86op_segovr_CS,
/*  0x2f */  i86op_das,

/*  0x30 */  i86op_xor_byte_RM_R,
/*  0x31 */  i86op_xor_word_RM_R,
/*  0x32 */  i86op_xor_byte_R_RM,
/*  0x33 */  i86op_xor_word_R_RM,
/*  0x34 */  i86op_xor_byte_AL_IMM,
/*  0x35 */  i86op_xor_word_AX_IMM,
/*  0x36 */  i86op_segovr_SS,
/*  0x37 */  i86op_aaa,

/*  0x38 */  i86op_cmp_byte_RM_R,
/*  0x39 */  i86op_cmp_word_RM_R,
/*  0x3a */  i86op_cmp_byte_R_RM,
/*  0x3b */  i86op_cmp_word_R_RM,
/*  0x3c */  i86op_cmp_byte_AL_IMM,
/*  0x3d */  i86op_cmp_word_AX_IMM,
/*  0x3e */  i86op_segovr_DS,
/*  0x3f */  i86op_aas,

/*  0x40 */  i86op_inc_AX,
/*  0x41 */  i86op_inc_CX,
/*  0x42 */  i86op_inc_DX,
/*  0x43 */  i86op_inc_BX,
/*  0x44 */  i86op_inc_SP,
/*  0x45 */  i86op_inc_BP,
/*  0x46 */  i86op_inc_SI,
/*  0x47 */  i86op_inc_DI,

/*  0x48 */  i86op_dec_AX,
/*  0x49 */  i86op_dec_CX,
/*  0x4a */  i86op_dec_DX,
/*  0x4b */  i86op_dec_BX,
/*  0x4c */  i86op_dec_SP,
/*  0x4d */  i86op_dec_BP,
/*  0x4e */  i86op_dec_SI,
/*  0x4f */  i86op_dec_DI,

/*  0x50 */  i86op_push_AX,
/*  0x51 */  i86op_push_CX,
/*  0x52 */  i86op_push_DX,
/*  0x53 */  i86op_push_BX,
/*  0x54 */  i86op_push_SP,
/*  0x55 */  i86op_push_BP,
/*  0x56 */  i86op_push_SI,
/*  0x57 */  i86op_push_DI,

/*  0x58 */  i86op_pop_AX,
/*  0x59 */  i86op_pop_CX,
/*  0x5a */  i86op_pop_DX,
/*  0x5b */  i86op_pop_BX,
/*  0x5c */  i86op_pop_SP,
/*  0x5d */  i86op_pop_BP,
/*  0x5e */  i86op_pop_SI,
/*  0x5f */  i86op_pop_DI,

/*  0x60 */  i86op_illegal_op,
/*  0x61 */  i86op_illegal_op,
/*  0x62 */  i86op_illegal_op,
/*  0x63 */  i86op_illegal_op,
/*  0x64 */  i86op_illegal_op,
/*  0x65 */  i86op_illegal_op,
/*  0x66 */  i86op_illegal_op,
/*  0x67 */  i86op_illegal_op,

/*  0x68 */  i86op_illegal_op,
/*  0x69 */  i86op_illegal_op,
/*  0x6a */  i86op_illegal_op,
/*  0x6b */  i86op_illegal_op,
/*  0x6c */  i86op_illegal_op,
/*  0x6d */  i86op_illegal_op,
/*  0x6e */  i86op_illegal_op,
/*  0x6f */  i86op_illegal_op,

/*  0x70 */  i86op_jump_near_O,
/*  0x71 */  i86op_jump_near_NO,
/*  0x72 */  i86op_jump_near_B,
/*  0x73 */  i86op_jump_near_NB,
/*  0x74 */  i86op_jump_near_Z,
/*  0x75 */  i86op_jump_near_NZ,
/*  0x76 */  i86op_jump_near_BE,
/*  0x77 */  i86op_jump_near_NBE,

/*  0x78 */  i86op_jump_near_S,
/*  0x79 */  i86op_jump_near_NS,
/*  0x7a */  i86op_jump_near_P,
/*  0x7b */  i86op_jump_near_NP,
/*  0x7c */  i86op_jump_near_L,
/*  0x7d */  i86op_jump_near_NL,
/*  0x7e */  i86op_jump_near_LE,
/*  0x7f */  i86op_jump_near_NLE,

/*  0x80 */  i86op_opc80_byte_RM_IMM,
/*  0x81 */  i86op_opc81_word_RM_IMM,
/*  0x82 */  i86op_opc82_byte_RM_IMM,
/*  0x83 */  i86op_opc83_word_RM_IMM,
/*  0x84 */  i86op_test_byte_RM_R,
/*  0x85 */  i86op_test_word_RM_R,
/*  0x86 */  i86op_xchg_byte_RM_R,
/*  0x87 */  i86op_xchg_word_RM_R,

/*  0x88 */  i86op_mov_byte_RM_R,
/*  0x89 */  i86op_mov_word_RM_R,
/*  0x8a */  i86op_mov_byte_R_RM,
/*  0x8b */  i86op_mov_word_R_RM,
/*  0x8c */  i86op_mov_word_RM_SR,
/*  0x8d */  i86op_lea_word_R_M,
/*  0x8e */  i86op_mov_word_SR_RM,
/*  0x8f */  i86op_pop_RM,

/*  0x90 */  i86op_nop,
/*  0x91 */  i86op_xchg_word_AX_CX,
/*  0x92 */  i86op_xchg_word_AX_DX,
/*  0x93 */  i86op_xchg_word_AX_BX,
/*  0x94 */  i86op_xchg_word_AX_SP,
/*  0x95 */  i86op_xchg_word_AX_BP  ,
/*  0x96 */  i86op_xchg_word_AX_SI   ,
/*  0x97 */  i86op_xchg_word_AX_DI    ,

/*  0x98 */  i86op_cbw,
/*  0x99 */  i86op_cwd,
/*  0x9a */  i86op_call_far_IMM,
/*  0x9b */  i86op_wait,
/*  0x9c */  i86op_pushf_word,
/*  0x9d */  i86op_popf_word,
/*  0x9e */  i86op_sahf,
/*  0x9f */  i86op_lahf,

/*  0xa0 */  i86op_mov_AL_M_IMM,
/*  0xa1 */  i86op_mov_AX_M_IMM,
/*  0xa2 */  i86op_mov_M_AL_IMM,
/*  0xa3 */  i86op_mov_M_AX_IMM,
/*  0xa4 */  i86op_movs_byte,
/*  0xa5 */  i86op_movs_word,
/*  0xa6 */  i86op_cmps_byte,
/*  0xa7 */  i86op_cmps_word,
/*  0xa8 */  i86op_test_AL_IMM,
/*  0xa9 */  i86op_test_AX_IMM,
/*  0xaa */  i86op_stos_byte,
/*  0xab */  i86op_stos_word,
/*  0xac */  i86op_lods_byte,
/*  0xad */  i86op_lods_word,
/*  0xac */  i86op_scas_byte,
/*  0xad */  i86op_scas_word,

/*  0xb0 */  i86op_mov_byte_AL_IMM,
/*  0xb1 */  i86op_mov_byte_CL_IMM,
/*  0xb2 */  i86op_mov_byte_DL_IMM,
/*  0xb3 */  i86op_mov_byte_BL_IMM,
/*  0xb4 */  i86op_mov_byte_AH_IMM,
/*  0xb5 */  i86op_mov_byte_CH_IMM,
/*  0xb6 */  i86op_mov_byte_DH_IMM,
/*  0xb7 */  i86op_mov_byte_BH_IMM,

/*  0xb8 */  i86op_mov_word_AX_IMM,
/*  0xb9 */  i86op_mov_word_CX_IMM,
/*  0xba */  i86op_mov_word_DX_IMM,
/*  0xbb */  i86op_mov_word_BX_IMM,
/*  0xbc */  i86op_mov_word_SP_IMM,
/*  0xbd */  i86op_mov_word_BP_IMM,
/*  0xbe */  i86op_mov_word_SI_IMM,
/*  0xbf */  i86op_mov_word_DI_IMM,

/*  0xc0 */  i86op_illegal_op,
/*  0xc1 */  i86op_illegal_op,
/*  0xc2 */  i86op_ret_near_IMM,
/*  0xc3 */  i86op_ret_near,
/*  0xc4 */  i86op_les_R_IMM,
/*  0xc5 */  i86op_lds_R_IMM,
/*  0xc6 */  i86op_mov_byte_RM_IMM,
/*  0xc7 */  i86op_mov_word_RM_IMM,
/*  0xc8 */  i86op_illegal_op,
/*  0xc9 */  i86op_illegal_op,
/*  0xca */  i86op_ret_far_IMM,
/*  0xcb */  i86op_ret_far,
/*  0xcc */  i86op_int3,
/*  0xcd */  i86op_int_IMM,
/*  0xce */  i86op_into,
/*  0xcf */  i86op_iret,

/*  0xd0 */  i86op_opcD0_byte_RM_1,
/*  0xd1 */  i86op_opcD1_word_RM_1,
/*  0xd2 */  i86op_opcD2_byte_RM_CL,
/*  0xd3 */  i86op_opcD3_word_RM_CL,
/*  0xd4 */  i86op_aam,
/*  0xd5 */  i86op_aad,
/*  0xd6 */  i86op_illegal_op,
/*  0xd7 */  i86op_xlat,
/*  0xd8 */  i86op_esc_coprocess_d8,
/*  0xd9 */  i86op_esc_coprocess_d9,
/*  0xda */  i86op_esc_coprocess_da,
/*  0xdb */  i86op_esc_coprocess_db,
/*  0xdc */  i86op_esc_coprocess_dc,
/*  0xdd */  i86op_esc_coprocess_dd,
/*  0xde */  i86op_esc_coprocess_de,
/*  0xdf */  i86op_esc_coprocess_df,

/*  0xe0 */  i86op_loopne,
/*  0xe1 */  i86op_loope,
/*  0xe2 */  i86op_loop,
/*  0xe3 */  i86op_jcxz,
/*  0xe4 */  i86op_in_byte_AL_IMM,
/*  0xe5 */  i86op_in_word_AX_IMM,
/*  0xe6 */  i86op_out_byte_IMM_AL,
/*  0xe7 */  i86op_out_word_IMM_AX,

/*  0xe8 */  i86op_call_near_IMM,
/*  0xe9 */  i86op_jump_near_IMM,
/*  0xea */  i86op_jump_far_IMM,
/*  0xeb */  i86op_jump_byte_IMM,
/*  0xec */  i86op_in_byte_AL_DX,
/*  0xed */  i86op_in_word_AX_DX,
/*  0xee */  i86op_out_byte_DX_AL,
/*  0xef */  i86op_out_word_DX_AX,

/*  0xf0 */  i86op_lock,
/*  0xf1 */  i86op_illegal_op,
/*  0xf2 */  i86op_repne,
/*  0xf3 */  i86op_repe,
/*  0xf4 */  i86op_halt,
/*  0xf5 */  i86op_cmc,
/*  0xf6 */  i86op_opcF6_byte_RM,
/*  0xf7 */  i86op_opcF7_word_RM,

/*  0xf8 */  i86op_clc,
/*  0xf9 */  i86op_stc,
/*  0xfa */  i86op_cli,
/*  0xfb */  i86op_sti,
/*  0xfc */  i86op_cld,
/*  0xfd */  i86op_std,
/*  0xfe */  i86op_opcFE_byte_RM,
/*  0xff */  i86op_opcFF_word_RM,

};
