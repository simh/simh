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

extern uint32 GetBYTEExtended(register uint32 Addr);
extern void PutBYTEExtended(register uint32 Addr, const register uint32 Value);

/* $Log: i86_prim_ops.c,v $
 * Revision 0.9  2003-01-10  23:33:10  jce
 * fixed some more warnings under gcc -Wall
 *
 * Revision 0.8  1992/04/11  21:58:13  hudgens
 * fixed some code causing warnings under gcc -Wall
 *
 * Revision 0.7  1991/07/30  02:04:34  hudgens
 * added copyright.
 *
 * Revision 0.6  1991/07/21  16:50:37  hudgens
 * fixed all flags in the bit shift and rotate instructions so that they
 * finally agree.
 * Also fixed the flags associated with IMUL and MUL instructions.
 *
 * Revision 0.5  1991/07/21  01:28:16  hudgens
 * added support for aad and aam primitives.
 *
 * Revision 0.4  1991/07/20  22:26:25  hudgens
 * fixed problem with sign extension in subroutine mem_access_word.
 *
 * Revision 0.3  1991/07/17  03:48:22  hudgens
 * fixed bugs having to do with the setting of flags in the
 * shift and rotate operations.  Also, fixed sign extension problem
 * with push_word and pop_word.
 *
 * Revision 0.2  1991/04/01  02:36:00  hudgens
 * Fixed several nasty bugs dealing with flag setting in the subroutines
 * sub_byte, sub_word, sbb_byte, sbb_word, and test_word.  The results
 * now agree with the PC on both of the testaopb and testaopw tests.
 *
 * Revision 0.1  1991/03/30  21:13:37  hudgens
 * Initial checkin.
 *
 *
 */

/* [JCE] Stop gcc -Wall complaining */
extern void i86_intr_raise(PC_ENV *m, uint8 intrnum);

/* the following table was generated using the following
   code (running on an IBM AT, Turbo C++ 2.0), for all values of i
   between 0 and 255.  AL is loaded with i's value, and then the
   operation "and al,al" sets the parity flag.  The flags are pushed
   onto the stack, and then popped back into AX.  Then AX is
   returned.  So the value of each table entry represents the
   parity of its index into the table.  This results in a somewhat
   faster mechanism for parity calculations than the straightforward
   method.
       andflags(i,res) int *res; {
          int flags;
          _AX = i;      asm and al,al;     asm pushf;     *res = _AX;
          asm pop ax;   flags = _AX;       return flags;
       }
 */

uint8 parity_tab[] = {
/*0*/  1, /*1*/  0, /*2*/  0, /*3*/  1,
/*4*/  0, /*5*/  1, /*6*/  1, /*7*/  0,
/*8*/  0, /*9*/  1, /*a*/  1, /*b*/  0,
/*c*/  1, /*d*/  0, /*e*/  0, /*f*/  1,
/*10*/  0, /*11*/  1, /*12*/  1, /*13*/  0,
/*14*/  1, /*15*/  0, /*16*/  0, /*17*/  1,
/*18*/  1, /*19*/  0, /*1a*/  0, /*1b*/  1,
/*1c*/  0, /*1d*/  1, /*1e*/  1, /*1f*/  0,
/*20*/  0, /*21*/  1, /*22*/  1, /*23*/  0,
/*24*/  1, /*25*/  0, /*26*/  0, /*27*/  1,
/*28*/  1, /*29*/  0, /*2a*/  0, /*2b*/  1,
/*2c*/  0, /*2d*/  1, /*2e*/  1, /*2f*/  0,
/*30*/  1, /*31*/  0, /*32*/  0, /*33*/  1,
/*34*/  0, /*35*/  1, /*36*/  1, /*37*/  0,
/*38*/  0, /*39*/  1, /*3a*/  1, /*3b*/  0,
/*3c*/  1, /*3d*/  0, /*3e*/  0, /*3f*/  1,
/*40*/  0, /*41*/  1, /*42*/  1, /*43*/  0,
/*44*/  1, /*45*/  0, /*46*/  0, /*47*/  1,
/*48*/  1, /*49*/  0, /*4a*/  0, /*4b*/  1,
/*4c*/  0, /*4d*/  1, /*4e*/  1, /*4f*/  0,
/*50*/  1, /*51*/  0, /*52*/  0, /*53*/  1,
/*54*/  0, /*55*/  1, /*56*/  1, /*57*/  0,
/*58*/  0, /*59*/  1, /*5a*/  1, /*5b*/  0,
/*5c*/  1, /*5d*/  0, /*5e*/  0, /*5f*/  1,
/*60*/  1, /*61*/  0, /*62*/  0, /*63*/  1,
/*64*/  0, /*65*/  1, /*66*/  1, /*67*/  0,
/*68*/  0, /*69*/  1, /*6a*/  1, /*6b*/  0,
/*6c*/  1, /*6d*/  0, /*6e*/  0, /*6f*/  1,
/*70*/  0, /*71*/  1, /*72*/  1, /*73*/  0,
/*74*/  1, /*75*/  0, /*76*/  0, /*77*/  1,
/*78*/  1, /*79*/  0, /*7a*/  0, /*7b*/  1,
/*7c*/  0, /*7d*/  1, /*7e*/  1, /*7f*/  0,
/*80*/  0, /*81*/  1, /*82*/  1, /*83*/  0,
/*84*/  1, /*85*/  0, /*86*/  0, /*87*/  1,
/*88*/  1, /*89*/  0, /*8a*/  0, /*8b*/  1,
/*8c*/  0, /*8d*/  1, /*8e*/  1, /*8f*/  0,
/*90*/  1, /*91*/  0, /*92*/  0, /*93*/  1,
/*94*/  0, /*95*/  1, /*96*/  1, /*97*/  0,
/*98*/  0, /*99*/  1, /*9a*/  1, /*9b*/  0,
/*9c*/  1, /*9d*/  0, /*9e*/  0, /*9f*/  1,
/*a0*/  1, /*a1*/  0, /*a2*/  0, /*a3*/  1,
/*a4*/  0, /*a5*/  1, /*a6*/  1, /*a7*/  0,
/*a8*/  0, /*a9*/  1, /*aa*/  1, /*ab*/  0,
/*ac*/  1, /*ad*/  0, /*ae*/  0, /*af*/  1,
/*b0*/  0, /*b1*/  1, /*b2*/  1, /*b3*/  0,
/*b4*/  1, /*b5*/  0, /*b6*/  0, /*b7*/  1,
/*b8*/  1, /*b9*/  0, /*ba*/  0, /*bb*/  1,
/*bc*/  0, /*bd*/  1, /*be*/  1, /*bf*/  0,
/*c0*/  1, /*c1*/  0, /*c2*/  0, /*c3*/  1,
/*c4*/  0, /*c5*/  1, /*c6*/  1, /*c7*/  0,
/*c8*/  0, /*c9*/  1, /*ca*/  1, /*cb*/  0,
/*cc*/  1, /*cd*/  0, /*ce*/  0, /*cf*/  1,
/*d0*/  0, /*d1*/  1, /*d2*/  1, /*d3*/  0,
/*d4*/  1, /*d5*/  0, /*d6*/  0, /*d7*/  1,
/*d8*/  1, /*d9*/  0, /*da*/  0, /*db*/  1,
/*dc*/  0, /*dd*/  1, /*de*/  1, /*df*/  0,
/*e0*/  0, /*e1*/  1, /*e2*/  1, /*e3*/  0,
/*e4*/  1, /*e5*/  0, /*e6*/  0, /*e7*/  1,
/*e8*/  1, /*e9*/  0, /*ea*/  0, /*eb*/  1,
/*ec*/  0, /*ed*/  1, /*ee*/  1, /*ef*/  0,
/*f0*/  1, /*f1*/  0, /*f2*/  0, /*f3*/  1,
/*f4*/  0, /*f5*/  1, /*f6*/  1, /*f7*/  0,
/*f8*/  0, /*f9*/  1, /*fa*/  1, /*fb*/  0,
/*fc*/  1, /*fd*/  0, /*fe*/  0, /*ff*/  1,
};

uint8 xor_0x3_tab[] = { 0, 1, 1, 0 };

/* CARRY CHAIN CALCULATION.
   This represents a somewhat expensive calculation which is
   apparently required to emulate the setting of the OF and
   AF flag.  The latter is not so important, but the former is.
   The overflow flag is the XOR of the top two bits of the
   carry chain for an addition (similar for subtraction).
   Since we do not want to simulate the addition in a bitwise
   manner, we try to calculate the carry chain given the
   two operands and the result.

   So, given the following table, which represents the
   addition of two bits, we can derive a formula for
   the carry chain.

       a   b   cin   r     cout
       0   0   0     0     0
       0   0   1     1     0
       0   1   0     1     0
       0   1   1     0     1
       1   0   0     1     0
       1   0   1     0     1
       1   1   0     0     1
       1   1   1     1     1

    Construction of table for cout:

               ab
         r  \  00   01   11  10
            |------------------
         0  |   0    1    1   1
         1  |   0    0    1   0

    By inspection, one gets:  cc = ab +  r'(a + b)

    That represents alot of operations, but NO CHOICE....

BORROW CHAIN CALCULATION.
   The following table represents the
   subtraction of two bits, from which we can derive a formula for
   the borrow chain.

       a   b   bin   r     bout
       0   0   0     0     0
       0   0   1     1     1
       0   1   0     1     1
       0   1   1     0     1
       1   0   0     1     0
       1   0   1     0     0
       1   1   0     0     0
       1   1   1     1     1

    Construction of table for cout:

               ab
         r  \  00   01   11  10
            |------------------
         0  |   0    1    0   0
         1  |   1    1    1   0

    By inspection, one gets:  bc = a'b +  r(a' + b)

 */

uint8 aad_word(PC_ENV *m, uint16 d)
{
    uint16 l;
    uint8  hb,lb;
    hb = (d>>8)&0xff;
    lb = (d&0xff);
    l = lb + 10 * hb;
    CONDITIONAL_SET_FLAG(l & 0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(l == 0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[l & 0xff], m, F_PF);
    return (uint8) l;
}

uint16 aam_word(PC_ENV *m, uint8 d)
{
    uint16 h,l;
    h = d / 10;
    l = d % 10;
    l |= (h<<8);
    CONDITIONAL_SET_FLAG(l & 0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(l == 0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[l & 0xff], m, F_PF);
    return l;
}

uint8 adc_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint16 res;         /* all operands in native machine order */
    register uint16 cc;
    if (ACCESS_FLAG(m,F_CF) )
      res = 1 + d + s;
    else
      res =  d + s;
    CONDITIONAL_SET_FLAG(res & 0x100, m, F_CF);
    CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(res & 0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (s & d) | ((~res) & (s | d));
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(cc>>6)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(cc&0x8, m, F_AF);
    return (uint8) res;
}

uint16 adc_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 cc;
    if (ACCESS_FLAG(m,F_CF) )
      res = 1 + d + s;
    else
      res =  d + s;
    /* set the carry flag to be bit 8 */
    CONDITIONAL_SET_FLAG(res & 0x10000, m, F_CF);
    CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(res & 0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (s & d) | ((~res) & (s | d));
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(cc>>14)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(cc&0x8, m, F_AF);
    return res;
}

/* Given   flags=f,  and bytes  d (dest)  and  s (source)
   perform the add and set the flags and the result back to
   *d.   USE NATIVE MACHINE ORDER...
*/
uint8 add_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint16 res;         /* all operands in native machine order */
    register uint16 cc;
    res = d + s;
    /* set the carry flag to be bit 8 */
    CONDITIONAL_SET_FLAG(res & 0x100, m, F_CF);
    CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(res & 0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (s & d) | ((~res) & (s | d));
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(cc>>6)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(cc&0x8, m, F_AF);
    return (uint8) res;
}

/* Given   flags=f,  and bytes  d (dest)  and  s (source)
   perform the add and set the flags and the result back to
   *d.   USE NATIVE MACHINE ORDER...
*/
uint16 add_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 cc;
    res = d + s;
    /* set the carry flag to be bit 8 */
    CONDITIONAL_SET_FLAG(res & 0x10000, m, F_CF);
    CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(res & 0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (s & d) | ((~res) & (s | d));
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(cc>>14)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(cc&0x8, m, F_AF);
    return res;
}

/*
   Flags m->R_FLG,  dest *d,  source  *s,  do a bitwise and of the
   source and destination, and then store back to the
   destination.  Size=byte.
*/
uint8 and_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint8 res;         /* all operands in native machine order */
    res = d & s;
    /* set the flags  */
    CLEAR_FLAG(m, F_OF);
    CLEAR_FLAG(m, F_CF);
    CONDITIONAL_SET_FLAG(res&0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(res==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res], m, F_PF);
    return res;
}

/*
   Flags m->R_FLG,  dest *d,  source  *s,  do a bitwise and of the
   source and destination, and then store back to the
   destination.  Size=byte.
*/
uint16 and_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint16 res;         /* all operands in native machine order */
    res = d & s;
    /* set the flags  */
    CLEAR_FLAG(m, F_OF);
    CLEAR_FLAG(m, F_CF);
    CONDITIONAL_SET_FLAG(res&0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG(res==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    return res;
}

uint8 cmp_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 bc;
    res = d - s;
    CLEAR_FLAG(m, F_CF);
    CONDITIONAL_SET_FLAG(res&0x80, m, F_SF);
    CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    CONDITIONAL_SET_FLAG(bc&0x80,m, F_CF);
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>6)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return d;  /* long story why this is needed.  Look at opcode
          0x80 in ops.c, for an idea why this is necessary.*/
}

uint16 cmp_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 bc;
    res = d - s;
    CONDITIONAL_SET_FLAG(res&0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    CONDITIONAL_SET_FLAG(bc&0x8000,m, F_CF);
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>14)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return d;
}

uint8 dec_byte(PC_ENV *m, uint8 d)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 bc;
    res = d - 1;
    CONDITIONAL_SET_FLAG(res&0x80, m, F_SF);
    CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the borrow chain.  See note at top */
    /* based on sub_byte, uses s==1.  */
    bc= (res&(~d|1))|(~d&1);
    /* carry flag unchanged */
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>6)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return res;
}

uint16 dec_word(PC_ENV *m, uint16 d)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 bc;
    res = d - 1;
    CONDITIONAL_SET_FLAG(res&0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the borrow chain.  See note at top */
    /* based on the sub_byte routine, with s==1 */
    bc= (res&(~d|1))|(~d&1);
    /* carry flag unchanged */
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>14)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return res;
}

/* Given   flags=f,  and byte  d (dest)
   perform the inc and set the flags and the result back to
   d.   USE NATIVE MACHINE ORDER...
*/
uint8 inc_byte(PC_ENV *m, uint8 d)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 cc;
    res = d + 1;
    CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(res & 0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  ((1 & d) | (~res)) & (1 | d);
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(cc>>6)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(cc&0x8, m, F_AF);
    return  res;
}

/* Given   flags=f,  and byte  d (dest)
   perform the inc and set the flags and the result back to
   *d.   USE NATIVE MACHINE ORDER...
*/
uint16 inc_word(PC_ENV *m, uint16 d)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 cc;
    res = d + 1;
    CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(res & 0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (1 & d) | ((~res) & (1 | d));
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(cc>>14)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(cc&0x8, m, F_AF);
    return res ;
}

uint8 or_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint8 res;         /* all operands in native machine order */
    res = d | s;
    CLEAR_FLAG(m, F_OF);
    CLEAR_FLAG(m, F_CF);
    CONDITIONAL_SET_FLAG(res&0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(res==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res], m, F_PF);
    return res;
}

uint16 or_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint16 res;         /* all operands in native machine order */
    res = d | s;
    /* set the carry flag to be bit 8 */
    CLEAR_FLAG(m, F_OF);
    CLEAR_FLAG(m, F_CF);
    CONDITIONAL_SET_FLAG(res&0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG(res==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    return res;
}

uint8 neg_byte(PC_ENV *m, uint8 s)
{
    register uint8 res;
    register uint8 bc;
    CONDITIONAL_SET_FLAG(s!=0, m, F_CF);
    res = -s;
    CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(res & 0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(parity_tab[res], m, F_PF);
    /* calculate the borrow chain --- modified such that d=0.
       substitutiing d=0 into     bc= res&(~d|s)|(~d&s);
       (the one used for sub) and simplifying, since ~d=0xff...,
       ~d|s == 0xffff..., and res&0xfff... == res.  Similarly
       ~d&s == s.  So the simplified result is:*/
    bc= res|s;
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>6)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return res;
}

uint16 neg_word(PC_ENV *m, uint16 s)
{
    register uint16 res;
    register uint16 bc;
    CONDITIONAL_SET_FLAG(s!=0, m, F_CF);
    res = -s;
    CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(res & 0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the borrow chain --- modified such that d=0.
       substitutiing d=0 into     bc= res&(~d|s)|(~d&s);
       (the one used for sub) and simplifying, since ~d=0xff...,
       ~d|s == 0xffff..., and res&0xfff... == res.  Similarly
       ~d&s == s.  So the simplified result is:*/
    bc= res|s;
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>6)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return res;
}

uint8 not_byte(PC_ENV *m, uint8 s)
{
    return ~s;
}

uint16 not_word(PC_ENV *m, uint16 s)
{
    return ~s;
}

/* access stuff from absolute location in memory.
   no segment registers are involved.
 */
uint16 mem_access_word(PC_ENV *m, int addr)
{
    /* Load in two steps.  Native byte order independent */
    return GetBYTEExtended(addr) | (GetBYTEExtended(addr + 1) << 8);
}

/*  given the register_set  r, and memory descriptor m,
    and word w, push w onto the stack.
    w ASSUMED IN NATIVE MACHINE ORDER.  Doesn't matter in this case???
 */
void push_word(PC_ENV *m, uint16 w)
{
    m->R_SP --;
    PutBYTEExtended((m->R_SS << 4) + m->R_SP, w >> 8);
    m->R_SP --;
    PutBYTEExtended((m->R_SS << 4) + m->R_SP, w & 0xff);
}

/*  given the  memory descriptor m,
    and word w, pop word from the stack.
 */
uint16 pop_word(PC_ENV *m)
{
    register uint16 res;
    res  = GetBYTEExtended((m->R_SS << 4) + m->R_SP);
    m->R_SP++;
    res |= GetBYTEExtended((m->R_SS << 4) + m->R_SP) << 8;
    m->R_SP++;
    return res;
}

/*****************************************************************
   BEGIN region consisting of bit shifts and rotates,
   much of which may be wrong.  Large hirsute factor.
*****************************************************************/
uint8 rcl_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint32  res, cnt, mask,cf;
    /* s is the rotate distance.  It varies from 0 - 8. */
    /* have
               CF  B_7 B_6 B_5 B_4 B_3 B_2 B_1 B_0
       want to rotate through the carry by "s" bits.  We could
       loop, but that's inefficient.  So the width is 9,
       and we split into three parts:
               The new carry flag   (was B_n)
           the stuff in B_n-1 .. B_0
           the stuff in B_7 .. B_n+1
       The new rotate is done mod 9, and given this,
       for a rotation of n bits (mod 9) the new carry flag is
       then located n bits from the MSB.  The low part is
       then shifted up cnt bits, and the high part is or'd
       in.  Using CAPS for new values, and lowercase for the
       original values, this can be expressed as:
           IF n > 0
               1) CF <-  b_(8-n)
           2) B_(7) .. B_(n)  <-  b_(8-(n+1)) .. b_0
           3) B_(n-1) <- cf
           4) B_(n-2) .. B_0 <-  b_7 .. b_(8-(n-1))
       I think this is correct.
       */
    res = d;
    /* [JCE] Extra brackets to stop gcc -Wall moaning */
    if ((cnt = s % 9))     /* not a typo, do nada if cnt==0 */
    {
        /* extract the new CARRY FLAG.  */
        /* CF <-  b_(8-n)               */
        cf =  (d >> (8-cnt)) & 0x1;
        /* get the low stuff which rotated
           into the range B_7 .. B_cnt */
        /* B_(7) .. B_(n)  <-  b_(8-(n+1)) .. b_0  */
        /* note that the right hand side done by the mask */
        res = (d << cnt) & 0xff;
        /* now the high stuff which rotated around
           into the positions B_cnt-2 .. B_0 */
        /* B_(n-2) .. B_0 <-  b_7 .. b_(8-(n-1)) */
        /* shift it downward, 7-(n-2) = 9-n positions.
           and mask off the result before or'ing in.
        */
        mask = (1<<(cnt-1)) - 1;
        res |= (d >> (9-cnt)) & mask;
        /* if the carry flag was set, or it in.  */
        if (ACCESS_FLAG(m,F_CF))   /* carry flag is set */
        {
            /*  B_(n-1) <- cf */
            res |=  1 << (cnt-1);
        }
        /* set the new carry flag, based on the variable "cf" */
        CONDITIONAL_SET_FLAG(cf, m, F_CF);
        /* OVERFLOW is set *IFF* cnt==1, then it is the
           xor of CF and the most significant bit.  Blecck. */
        /* parenthesized this expression since it appears to
           be causing OF to be missed. */
        CONDITIONAL_SET_FLAG(cnt==1&&xor_0x3_tab[cf+((res>>6)&0x2)], m, F_OF);
    }
    return res & 0xff;
}

uint16  rcl_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint32  res, cnt, mask,cf;
    /* see analysis above. */
    /* width here is 16 bits + carry bit */
    res = d;
    /* [JCE] Extra brackets to stop gcc -Wall moaning */
    if ((cnt = s % 17))     /* not a typo, do nada if cnt==0 */
    {
        /* extract the new CARRY FLAG. */
        /* CF <-  b_(16-n)             */
        cf =  (d >> (16-cnt)) & 0x1;
        /* get the low stuff which rotated
           into the range B_15 .. B_cnt */
        /* B_(15) .. B_(n)  <-  b_(16-(n+1)) .. b_0  */
        /* note that the right hand side done by the mask */
        res = (d << cnt) & 0xffff;
        /* now the high stuff which rotated around
           into the positions B_cnt-2 .. B_0 */
        /* B_(n-2) .. B_0 <-  b_15 .. b_(16-(n-1)) */
        /* shift it downward, 15-(n-2) = 17-n positions.
           and mask off the result before or'ing in.
         */
        mask = (1<<(cnt-1)) - 1;
        res |= (d >> (17-cnt)) & mask;
        /* if the carry flag was set, or it in.  */
        if (ACCESS_FLAG(m, F_CF))   /* carry flag is set */
        {
            /*  B_(n-1) <- cf */
            res |=  1 << (cnt-1);
        }
        /* set the new carry flag, based on the variable "cf" */
        CONDITIONAL_SET_FLAG(cf, m, F_CF);
        /* OVERFLOW is set *IFF* cnt==1, then it is the
           xor of CF and the most significant bit.  Blecck.
           Note that we're forming a 2 bit word here to index
           into the table. The expression cf+(res>>14)&0x2
           represents the two bit word b_15 CF.
        */
        /* parenthesized following expression... */
        CONDITIONAL_SET_FLAG(cnt==1&&xor_0x3_tab[cf+((res>>14)&0x2)], m, F_OF);
    }
    return res & 0xffff;
}

uint8 rcr_byte(PC_ENV *m, uint8 d, uint8 s)
{
    uint8 res, cnt;
    uint8 mask, cf, ocf = 0;
    /* rotate right through carry */
    /*
        s is the rotate distance.  It varies from 0 - 8.
        d is the byte object rotated.
        have
        CF  B_7 B_6 B_5 B_4 B_3 B_2 B_1 B_0
        The new rotate is done mod 9, and given this,
        for a rotation of n bits (mod 9) the new carry flag is
        then located n bits from the LSB.  The low part is
        then shifted up cnt bits, and the high part is or'd
        in.  Using CAPS for new values, and lowercase for the
        original values, this can be expressed as:
        IF n > 0
        1) CF <-  b_(n-1)
        2) B_(8-(n+1)) .. B_(0)  <-  b_(7) .. b_(n)
        3) B_(8-n) <- cf
        4) B_(7) .. B_(8-(n-1)) <-  b_(n-2) .. b_(0)
        I think this is correct.
    */
    res = d;
    /* [JCE] Extra brackets to stop gcc -Wall moaning */
    if ((cnt = s % 9))     /* not a typo, do nada if cnt==0 */
    {
        /* extract the new CARRY FLAG. */
        /* CF <-  b_(n-1)              */
        if (cnt == 1)
        {
            cf = d & 0x1;
            /* note hackery here.  Access_flag(..) evaluates to either
               0 if flag not set
               non-zero if flag is set.
               doing access_flag(..) != 0 casts that into either
               0..1 in any representation of the flags register
               (i.e. packed bit array or unpacked.)
            */
            ocf = ACCESS_FLAG(m,F_CF) != 0;
        }
        else
            cf =  (d >> (cnt-1)) & 0x1;
        /* B_(8-(n+1)) .. B_(0)  <-  b_(7) .. b_n  */
        /* note that the right hand side done by the mask
        This is effectively done by shifting the
        object to the right.  The result must be masked,
        in case the object came in and was treated
        as a negative number.  Needed???*/
        mask = (1<<(8-cnt))-1;
        res = (d >> cnt) & mask;
        /* now the high stuff which rotated around
           into the positions B_cnt-2 .. B_0 */
        /* B_(7) .. B_(8-(n-1)) <-  b_(n-2) .. b_(0) */
        /* shift it downward, 7-(n-2) = 9-n positions.
           and mask off the result before or'ing in.
        */
        res |= (d << (9-cnt));
        /* if the carry flag was set, or it in.  */
        if (ACCESS_FLAG(m,F_CF))   /* carry flag is set */
        {
            /*  B_(8-n) <- cf */
            res |=  1 << (8 - cnt);
        }
        /* set the new carry flag, based on the variable "cf" */
        CONDITIONAL_SET_FLAG(cf, m, F_CF);
        /* OVERFLOW is set *IFF* cnt==1, then it is the
        xor of CF and the most significant bit.  Blecck. */
        /* parenthesized... */
        if (cnt == 1)
        { /* [JCE] Explicit braces to stop gcc -Wall moaning */
            CONDITIONAL_SET_FLAG(xor_0x3_tab[ocf+((d>>6)&0x2)], m, F_OF);
        }
    }
    return res;
}

uint16 rcr_word(PC_ENV *m, uint16 d, uint16 s)
{
    uint16 res, cnt;
    uint16 mask, cf, ocf = 0;
    /* rotate right through carry */
    /*
        s is the rotate distance.  It varies from 0 - 8.
        d is the byte object rotated.
        have
        CF  B_15   ...   B_0
        The new rotate is done mod 17, and given this,
        for a rotation of n bits (mod 17) the new carry flag is
        then located n bits from the LSB.  The low part is
        then shifted up cnt bits, and the high part is or'd
        in.  Using CAPS for new values, and lowercase for the
        original values, this can be expressed as:
        IF n > 0
        1) CF <-  b_(n-1)
        2) B_(16-(n+1)) .. B_(0)  <-  b_(15) .. b_(n)
        3) B_(16-n) <- cf
        4) B_(15) .. B_(16-(n-1)) <-  b_(n-2) .. b_(0)
        I think this is correct.
    */
    res = d;
    /* [JCE] Extra brackets to stop gcc -Wall moaning */
    if ((cnt = s % 17))     /* not a typo, do nada if cnt==0 */
    {
        /* extract the new CARRY FLAG. */
        /* CF <-  b_(n-1)              */
        if (cnt==1)
        {
            cf = d & 0x1;
            /* see note above on teh byte version */
            ocf = ACCESS_FLAG(m,F_CF) != 0;
        }
        else
            cf =  (d >> (cnt-1)) & 0x1;
        /* B_(16-(n+1)) .. B_(0)  <-  b_(15) .. b_n  */
        /* note that the right hand side done by the mask
           This is effectively done by shifting the
           object to the right.  The result must be masked,
           in case the object came in and was treated
           as a negative number.  Needed???*/
        mask = (1<<(16-cnt))-1;
        res = (d >> cnt) & mask;
        /* now the high stuff which rotated around
           into the positions B_cnt-2 .. B_0 */
        /* B_(15) .. B_(16-(n-1)) <-  b_(n-2) .. b_(0) */
        /* shift it downward, 15-(n-2) = 17-n positions.
           and mask off the result before or'ing in.
        */
        res |= (d << (17-cnt));
        /* if the carry flag was set, or it in.  */
        if (ACCESS_FLAG(m,F_CF))   /* carry flag is set */
        {
            /*  B_(16-n) <- cf */
            res |=  1 << (16 - cnt);
        }
        /* set the new carry flag, based on the variable "cf" */
        CONDITIONAL_SET_FLAG(cf, m, F_CF);
        /* OVERFLOW is set *IFF* cnt==1, then it is the
           xor of CF and the most significant bit.  Blecck. */
        if (cnt==1)
        { /* [JCE] Explicit braces to stop gcc -Wall moaning */
            CONDITIONAL_SET_FLAG(xor_0x3_tab[ocf+((d>>14)&0x2)], m, F_OF);
        }
    }
    return res;
}

uint8 rol_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint32  res, cnt, mask;
    /* rotate left */
    /*
      s is the rotate distance.  It varies from 0 - 8.
      d is the byte object rotated.
      have
               CF  B_7 ... B_0
      The new rotate is done mod 8.
      Much simpler than the "rcl" or "rcr" operations.
           IF n > 0
           1) B_(7) .. B_(n)  <-  b_(8-(n+1)) .. b_(0)
           2) B_(n-1) .. B_(0) <-  b_(7) .. b_(8-n)
      I think this is correct.
    */
    res =d;
    /* [JCE] Extra brackets to stop gcc -Wall moaning */
    if ((cnt = s % 8))     /* not a typo, do nada if cnt==0 */
    {
       /* B_(7) .. B_(n)  <-  b_(8-(n+1)) .. b_(0) */
       res = (d << cnt);
       /* B_(n-1) .. B_(0) <-  b_(7) .. b_(8-n) */
       mask = (1 << cnt) - 1;
       res |= (d >> (8-cnt)) & mask;
       /* set the new carry flag, Note that it is the low order
          bit of the result!!!                               */
       CONDITIONAL_SET_FLAG(res&0x1, m, F_CF);
       /* OVERFLOW is set *IFF* cnt==1, then it is the
          xor of CF and the most significant bit.  Blecck. */
       CONDITIONAL_SET_FLAG(cnt==1&&xor_0x3_tab[(res&0x1)+((res>>6)&0x2)], m, F_OF);
    }
    return res&0xff;
}

uint16 rol_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint32  res, cnt, mask;
    /* rotate left */
    /*
      s is the rotate distance.  It varies from 0 - 8.
      d is the byte object rotated.
      have
               CF  B_15 ... B_0
      The new rotate is done mod 8.
      Much simpler than the "rcl" or "rcr" operations.
           IF n > 0
           1) B_(15) .. B_(n)  <-  b_(16-(n+1)) .. b_(0)
           2) B_(n-1) .. B_(0) <-  b_(16) .. b_(16-n)
      I think this is correct.
    */
    res = d;
    /* [JCE] Extra brackets to stop gcc -Wall moaning */
    if ((cnt = s % 16))     /* not a typo, do nada if cnt==0 */
    {
       /* B_(16) .. B_(n)  <-  b_(16-(n+1)) .. b_(0) */
       res = (d << cnt);
       /* B_(n-1) .. B_(0) <-  b_(15) .. b_(16-n) */
       mask = (1 << cnt) - 1;
       res |= (d >> (16-cnt)) & mask;
       /* set the new carry flag, Note that it is the low order
          bit of the result!!!                               */
       CONDITIONAL_SET_FLAG(res&0x1, m, F_CF);
       /* OVERFLOW is set *IFF* cnt==1, then it is the
          xor of CF and the most significant bit.  Blecck. */
       CONDITIONAL_SET_FLAG(cnt==1&&xor_0x3_tab[(res&0x1)+((res>>14)&0x2)], m, F_OF);
    }
    return res&0xffff;
}

uint8 ror_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint32  res, cnt, mask;
    /* rotate right */
    /*
      s is the rotate distance.  It varies from 0 - 8.
      d is the byte object rotated.
      have
               B_7 ... B_0
      The rotate is done mod 8.
           IF n > 0
           1) B_(8-(n+1)) .. B_(0)  <-  b_(7) .. b_(n)
           2) B_(7) .. B_(8-n) <-  b_(n-1) .. b_(0)
    */
    res = d;
    /* [JCE] Extra brackets to stop gcc -Wall moaning */
    if ((cnt = s % 8))     /* not a typo, do nada if cnt==0 */
    {
       /* B_(7) .. B_(8-n) <-  b_(n-1) .. b_(0)*/
       res = (d << (8-cnt));
       /* B_(8-(n+1)) .. B_(0)  <-  b_(7) .. b_(n) */
       mask = (1 << (8-cnt)) - 1;
       res |= (d >> (cnt)) & mask;
       /* set the new carry flag, Note that it is the low order
          bit of the result!!!                               */
       CONDITIONAL_SET_FLAG(res&0x80, m, F_CF);
       /* OVERFLOW is set *IFF* cnt==1, then it is the
          xor of the two most significant bits.  Blecck. */
       CONDITIONAL_SET_FLAG(cnt==1&& xor_0x3_tab[(res>>6)&0x3], m, F_OF);
    }
    return res&0xff;
}

uint16 ror_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint32  res, cnt, mask;
    /* rotate right */
    /*
      s is the rotate distance.  It varies from 0 - 8.
      d is the byte object rotated.
      have
               B_15 ... B_0
      The rotate is done mod 16.
           IF n > 0
           1) B_(16-(n+1)) .. B_(0)  <-  b_(15) .. b_(n)
           2) B_(15) .. B_(16-n) <-  b_(n-1) .. b_(0)
      I think this is correct.
    */
    res =d ;
    /* [JCE] Extra brackets to stop gcc -Wall moaning */
    if ((cnt = s % 16))     /* not a typo, do nada if cnt==0 */
    {
       /* B_(15) .. B_(16-n) <-  b_(n-1) .. b_(0)*/
       res = (d << (16-cnt));
       /* B_(16-(n+1)) .. B_(0)  <-  b_(15) .. b_(n) */
       mask = (1 << (16-cnt)) - 1;
       res |= (d >> (cnt)) & mask;
       /* set the new carry flag, Note that it is the low order
          bit of the result!!!                               */
       CONDITIONAL_SET_FLAG(res&0x8000, m, F_CF);
       /* OVERFLOW is set *IFF* cnt==1, then it is the
          xor of CF and the most significant bit.  Blecck. */
       CONDITIONAL_SET_FLAG(cnt==1  && xor_0x3_tab[(res>>14)&0x3], m, F_OF);
    }
    return res & 0xffff;
}

uint8 shl_byte(PC_ENV *m, uint8 d, uint8 s)
{
    uint32 cnt,res,cf;
    if (s < 8)
    {
        cnt = s % 8;
        /* last bit shifted out goes into carry flag */
        if (cnt>0)
        {
            res = d << cnt;
            cf = d & (1<<(8-cnt));
            CONDITIONAL_SET_FLAG(cf, m, F_CF);
            CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
            CONDITIONAL_SET_FLAG(res & 0x80, m, F_SF);
            CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
        }
        else
        {
            res = (uint8)d;
        }
        if (cnt == 1)
        {
            /* Needs simplification. */
            CONDITIONAL_SET_FLAG(
            (((res&0x80)==0x80) ^
            (ACCESS_FLAG(m,F_CF) != 0)) ,
            /* was (m->R_FLG&F_CF)==F_CF)), */
            m, F_OF);
        }
        else
        {
            CLEAR_FLAG(m,F_OF);
        }
    }
    else
    {
        res = 0;
/*      CLEAR_FLAG(m,F_CF);*/
        CONDITIONAL_SET_FLAG((s == 8) && (d & 1), m, F_CF); /* Peter Schorn bug fix */
        CLEAR_FLAG(m,F_OF);
        CLEAR_FLAG(m,F_SF);
        CLEAR_FLAG(m,F_PF);
        SET_FLAG(m,F_ZF);
    }
    return res & 0xff;
}

uint16 shl_word(PC_ENV *m, uint16 d, uint16 s)
{
    uint32 cnt,res,cf;
    if (s < 16)
    {
        cnt = s % 16;
        if (cnt > 0)
        {
            res = d << cnt;
            /* last bit shifted out goes into carry flag */
            cf = d & (1<<(16-cnt));
            CONDITIONAL_SET_FLAG(cf, m, F_CF);
            CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
            CONDITIONAL_SET_FLAG(res & 0x8000, m, F_SF);
            CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
        }
        else
        {
            res = (uint16)d;
        }
        if (cnt == 1)
        {
            /* Needs simplification. */
            CONDITIONAL_SET_FLAG(
            (((res&0x8000)==0x8000) ^
            (ACCESS_FLAG(m,F_CF) != 0)),
            /*((m&F_CF)==F_CF)),*/
            m, F_OF);
        }
        else
        {
            CLEAR_FLAG(m,F_OF);
        }
    }
    else
    {
        res = 0;
        /*      CLEAR_FLAG(m,F_CF);*/
        CONDITIONAL_SET_FLAG((s == 16) && (d & 1), m, F_CF); /* Peter Schorn bug fix */
        CLEAR_FLAG(m,F_OF);
        SET_FLAG(m,F_ZF);
        CLEAR_FLAG(m,F_SF);
        CLEAR_FLAG(m,F_PF);
    }
    return res & 0xffff;
}

uint8 shr_byte(PC_ENV *m, uint8 d, uint8 s)
{
    uint32 cnt,res,cf,mask;
    if (s < 8)
    {
        cnt = s % 8;
        if (cnt > 0)
        {
            mask = (1<<(8-cnt))-1;
            cf = d & (1<<(cnt-1));
            res = (d >> cnt) & mask;
            CONDITIONAL_SET_FLAG(cf, m, F_CF);
            CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
            CONDITIONAL_SET_FLAG(res & 0x80, m, F_SF);
            CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
        }
        else
        {
            res = (uint8) d;
        }
        if (cnt == 1)
        {
            CONDITIONAL_SET_FLAG(xor_0x3_tab[(res>>6)&0x3], m, F_OF);
        }
        else
        {
            CLEAR_FLAG(m,F_OF);
        }
    }
    else
    {
        res = 0;
        /*      CLEAR_FLAG(m,F_CF);*/
        CONDITIONAL_SET_FLAG((s == 8) && (d & 0x80), m, F_CF); /* Peter Schorn bug fix */
        CLEAR_FLAG(m,F_OF);
        SET_FLAG(m,F_ZF);
        CLEAR_FLAG(m,F_SF);
        CLEAR_FLAG(m,F_PF);
    }
    return res & 0xff;
}

uint16 shr_word(PC_ENV *m, uint16 d, uint16 s)
{
    uint32 cnt,res,cf,mask;
    if (s < 16)
    {
        cnt = s % 16;
        if (cnt > 0)
        {
            mask = (1<<(16-cnt))-1;
            cf = d & (1<<(cnt-1));
            res = (d >> cnt) & mask;
            CONDITIONAL_SET_FLAG(cf, m, F_CF);
            CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
            CONDITIONAL_SET_FLAG(res & 0x8000, m, F_SF);
            CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
        }
        else
        {
            res = d;
        }
        if (cnt == 1)
        {
            CONDITIONAL_SET_FLAG(xor_0x3_tab[(res>>14)&0x3], m, F_OF);
        }
        else
        {
            CLEAR_FLAG(m,F_OF);
        }
    }
    else
    {
        res = 0;
        /*      CLEAR_FLAG(m,F_CF);*/
        CONDITIONAL_SET_FLAG((s == 16) && (d & 0x8000), m, F_CF); /* Peter Schorn bug fix */
        CLEAR_FLAG(m,F_OF);
        SET_FLAG(m,F_ZF);
        CLEAR_FLAG(m,F_SF);
        CLEAR_FLAG(m,F_PF);
    }
    return res & 0xffff;
}

/* XXXX ??? flags may be wrong??? */
uint8 sar_byte(PC_ENV *m, uint8 d, uint8 s)
{
    uint32 cnt,res,cf,mask,sf;
    res = d;
    sf = d & 0x80;
    cnt = s % 8;
    if (cnt > 0 && cnt < 8)
    {
        mask = (1<<(8-cnt))-1;
        cf = d & (1<<(cnt-1));
        res = (d >> cnt) & mask;
        CONDITIONAL_SET_FLAG(cf, m, F_CF);
        if (sf)
        {
            res |= ~mask;
        }
        CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
        CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
        CONDITIONAL_SET_FLAG(res & 0x80, m, F_SF);
    }
    else if (cnt >= 8)
    {
        if (sf)
        {
            res = 0xff;
            SET_FLAG(m,F_CF);
            CLEAR_FLAG(m,F_ZF);
            SET_FLAG(m, F_SF);
            SET_FLAG(m, F_PF);
        }
        else
        {
            res = 0;
            CLEAR_FLAG(m,F_CF);
            SET_FLAG(m,F_ZF);
            CLEAR_FLAG(m, F_SF);
            CLEAR_FLAG(m, F_PF);
        }
    }
    return res&0xff;
}

uint16 sar_word(PC_ENV *m, uint16 d, uint16 s)
{
    uint32 cnt, res, cf, mask, sf;
    sf = d & 0x8000;
    cnt = s % 16;
    res = d;
    if (cnt > 0 && cnt < 16)
    {
        mask = (1<<(16-cnt))-1;
        cf = d & (1<<(cnt-1));
        res = (d >> cnt) & mask;
        CONDITIONAL_SET_FLAG(cf, m, F_CF);
        if (sf)
        {
            res |= ~mask;
        }
        CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
        CONDITIONAL_SET_FLAG(res & 0x8000, m, F_SF);
        CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    }
    else if (cnt >= 16)
    {
        if (sf)
        {
            res = 0xffff;
            SET_FLAG(m,F_CF);
            CLEAR_FLAG(m,F_ZF);
            SET_FLAG(m, F_SF);
            SET_FLAG(m, F_PF);
        }
        else
        {
            res = 0;
            CLEAR_FLAG(m,F_CF);
            SET_FLAG(m,F_ZF);
            CLEAR_FLAG(m, F_SF);
            CLEAR_FLAG(m, F_PF);
        }
    }
    return res & 0xffff;
}

uint8 sbb_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 bc;
    if (ACCESS_FLAG(m,F_CF) )
      res = d - s - 1;
    else
      res =  d - s;
    CONDITIONAL_SET_FLAG(res&0x80, m, F_SF);
    CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    CONDITIONAL_SET_FLAG(bc&0x80,m, F_CF);
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>6)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return res & 0xff;
}

uint16 sbb_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 bc;
    if (ACCESS_FLAG(m,F_CF))
      res = d - s - 1;
    else
      res =  d - s;
    CONDITIONAL_SET_FLAG(res&0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    CONDITIONAL_SET_FLAG(bc&0x8000,m, F_CF);
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>14)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return res & 0xffff;
}

uint8 sub_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 bc;
    res = d - s;
    CONDITIONAL_SET_FLAG(res&0x80, m, F_SF);
    CONDITIONAL_SET_FLAG((res&0xff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    CONDITIONAL_SET_FLAG(bc&0x80,m, F_CF);
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>6)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return res & 0xff;
}

uint16 sub_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint32 res;         /* all operands in native machine order */
    register uint32 bc;
    res = d - s;
    CONDITIONAL_SET_FLAG(res&0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG((res&0xffff)==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    CONDITIONAL_SET_FLAG(bc&0x8000,m, F_CF);
    CONDITIONAL_SET_FLAG(xor_0x3_tab[(bc>>14)&0x3], m, F_OF);
    CONDITIONAL_SET_FLAG(bc&0x8, m, F_AF);
    return res & 0xffff;
}

void test_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint32 res;         /* all operands in native machine order */
    res = d & s;
    CLEAR_FLAG(m, F_OF);
    CONDITIONAL_SET_FLAG(res&0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(res==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* AF == dont care*/
    CLEAR_FLAG(m, F_CF);
}

void test_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint32 res;         /* all operands in native machine order */
    res = d & s;
    CLEAR_FLAG(m, F_OF);
    CONDITIONAL_SET_FLAG(res&0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG(res==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    /* AF == dont care*/
    CLEAR_FLAG(m, F_CF);
}

uint8 xor_byte(PC_ENV *m, uint8 d, uint8 s)
{
    register uint8 res;         /* all operands in native machine order */
    res = d ^ s;
    CLEAR_FLAG(m, F_OF);
    CONDITIONAL_SET_FLAG(res&0x80, m, F_SF);
    CONDITIONAL_SET_FLAG(res==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res], m, F_PF);
    CLEAR_FLAG(m, F_CF);
    return res;
}

uint16 xor_word(PC_ENV *m, uint16 d, uint16 s)
{
    register uint16 res;         /* all operands in native machine order */
    res = d ^ s;
    /* set the carry flag to be bit 8 */
    CLEAR_FLAG(m, F_OF);
    CONDITIONAL_SET_FLAG(res&0x8000, m, F_SF);
    CONDITIONAL_SET_FLAG(res==0, m, F_ZF);
    CONDITIONAL_SET_FLAG(parity_tab[res&0xff], m, F_PF);
    CLEAR_FLAG(m, F_CF);
    return res;
}

void imul_byte(PC_ENV *m, uint8 s)
{
    int16 res = (int8)m->R_AL * (int8)s;
    m->R_AX = res;
    /* Undef --- Can't hurt */
    CONDITIONAL_SET_FLAG(res&0x8000,m,F_SF);
    CONDITIONAL_SET_FLAG(res==0,m,F_ZF);
    if (m->R_AH == 0 || m->R_AH == 0xff)
    {
       CLEAR_FLAG(m, F_CF);
       CLEAR_FLAG(m, F_OF);
    }
    else
    {
       SET_FLAG(m, F_CF);
       SET_FLAG(m, F_OF);
    }
}

void imul_word(PC_ENV *m, uint16 s)
{
    int32 res = (int16)m->R_AX * (int16)s;
    m->R_AX = res & 0xffff;
    m->R_DX = (res >> 16) & 0xffff;
    /* Undef --- Can't hurt */
    CONDITIONAL_SET_FLAG(res&0x80000000,m,F_SF);
    CONDITIONAL_SET_FLAG(res==0,m,F_ZF);
    if (m->R_DX == 0 || m->R_DX == 0xffff)
    {
       CLEAR_FLAG(m, F_CF);
       CLEAR_FLAG(m, F_OF);
    }
    else
    {
       SET_FLAG(m, F_CF);
       SET_FLAG(m, F_OF);
    }
}

void mul_byte(PC_ENV *m, uint8 s)
{
    uint16 res = m->R_AL * s;
    m->R_AX = res;
    /* Undef --- Can't hurt */
    CLEAR_FLAG(m,F_SF);
    CONDITIONAL_SET_FLAG(res==0,m,F_ZF);
    if (m->R_AH == 0)
    {
       CLEAR_FLAG(m, F_CF);
       CLEAR_FLAG(m, F_OF);
    }
    else
    {
       SET_FLAG(m, F_CF);
       SET_FLAG(m, F_OF);
    }
}

void mul_word(PC_ENV *m, uint16 s)
{
    uint32 res = m->R_AX * s;
    /* Undef --- Can't hurt */
    CLEAR_FLAG(m,F_SF);
    CONDITIONAL_SET_FLAG(res==0,m,F_ZF);
    m->R_AX = res & 0xffff;
    m->R_DX = (res >> 16) & 0xffff;
    if (m->R_DX == 0)
    {
       CLEAR_FLAG(m, F_CF);
       CLEAR_FLAG(m, F_OF);
    }
    else
    {
       SET_FLAG(m, F_CF);
       SET_FLAG(m, F_OF);
    }
}

void idiv_byte(PC_ENV *m, uint8 s)
{
    int32 dvd,div,mod;
    dvd = (int16)m->R_AX;
    if (s == 0)
    {
       i86_intr_raise(m,0);
       return;
    }
    div = dvd / (int8)s;
    mod = dvd % (int8)s;
    if (abs(div) > 0x7f)
    {
       i86_intr_raise(m,0);
       return;
    }
    /* Undef --- Can't hurt */
    CONDITIONAL_SET_FLAG(div&0x80,m,F_SF);
    CONDITIONAL_SET_FLAG(div==0,m,F_ZF);
    m->R_AL = (int8)div;
    m->R_AH = (int8)mod;
}

void idiv_word(PC_ENV *m, uint16 s)
{
    int32 dvd,dvs,div,mod;
    dvd = m->R_DX;
    dvd = (dvd << 16) | m->R_AX;
    if (s == 0)
    {
       i86_intr_raise(m,0);
       return;
    }
    dvs = (int16)s;
    div = dvd / dvs;
    mod = dvd % dvs;
    if (abs(div) > 0x7fff)
    {
       i86_intr_raise(m,0);
       return;
    }
    /* Undef --- Can't hurt */
    CONDITIONAL_SET_FLAG(div&0x8000,m,F_SF);
    CONDITIONAL_SET_FLAG(div==0,m,F_ZF);
/*    debug_printf(m, "\n%d/%d=%d,%d\n",dvd,dvs,div,mod); */
    m->R_AX = div;
    m->R_DX = mod;
}

void div_byte(PC_ENV *m, uint8 s)
{
    uint32 dvd,dvs,div,mod;
    dvs = s;
    dvd =  m->R_AX;
    if (s == 0)
    {
       i86_intr_raise(m,0);
       return;
    }
    div = dvd / dvs;
    mod = dvd % dvs;
    if (div > 0xff)     // psco changed from if (abs(div) > 0xff)
    {
       i86_intr_raise(m,0);
       return;
    }
    /* Undef --- Can't hurt */
    CLEAR_FLAG(m,F_SF);
    CONDITIONAL_SET_FLAG(div==0,m,F_ZF);
    m->R_AL = (uint8)div;
    m->R_AH = (uint8)mod;
}

void div_word(PC_ENV *m, uint16 s)
{
    uint32 dvd,dvs,div,mod;
    dvd = m->R_DX;
    dvd = (dvd << 16) | m->R_AX;
    dvs = s;
    if (dvs == 0)
    {
       i86_intr_raise(m,0);
       return;
    }
    div = dvd / dvs;
    mod = dvd % dvs;
/*    sim_printf("dvd=%x dvs=%x -> div=%x mod=%x\n",dvd, dvs,div, mod);*/
    if (div > 0xffff)  // psco changed from if (abs(div) > 0xffff)
    {
       i86_intr_raise(m,0);
       return;
    }
    /* Undef --- Can't hurt */
    CLEAR_FLAG(m,F_SF);
    CONDITIONAL_SET_FLAG(div==0,m,F_ZF);
    m->R_AX = div;
    m->R_DX = mod;
}
