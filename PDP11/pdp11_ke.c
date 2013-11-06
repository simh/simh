/* pdp11_ke.c: PDP-11/20 extended arithmetic element

   Copyright (c) 1993-2008, Robert M Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ke_ACTION OF CONTRke_ACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   This code draws on prior work by Tim Shoppa and Brad Parker. My thanks for
   to them for letting me use their work.

   EAE          PDP-11/20 extended arithmetic element
*/

#include "pdp11_defs.h"

#define GET_SIGN_L(v)   (((v) >> 31) & 1)
#define GET_SIGN_W(v)   (((v) >> 15) & 1)
#define GET_SIGN_B(v)   (((v) >> 7) & 1)

/* KE11A I/O address offsets 0177300 - 0177316 */

#define KE_DIV          000                             /* divide */
#define KE_AC           002                             /* accumulator */
#define KE_MQ           004                             /* MQ */
#define KE_MUL          006                             /* multiply */
#define KE_SC           010                             /* step counter */
#define KE_NOR          012                             /* normalize */
#define KE_LSH          014                             /* logical shift */
#define KE_ASH          016                             /* arithmetic shift */

/* Status register */

#define KE_SR_C         0001                            /* carry */
#define KE_SR_SXT       0002                            /* AC<15:0> = MQ<15> */
#define KE_SR_Z         0004                            /* AC = MQ = 0 */
#define KE_SR_MQZ       0010                            /* MQ = 0 */
#define KE_SR_ACZ       0020                            /* AC = 0 */
#define KE_SR_ACM1      0040                            /* AC = 177777 */
#define KE_SR_N         0100                            /* last op negative */
#define KE_SR_NXV       0200                            /* last op ovf XOR N */
#define KE_SR_DYN       (KE_SR_SXT|KE_SR_Z|KE_SR_MQZ|KE_SR_ACZ|KE_SR_ACM1)

/* Visible state */

uint32 ke_AC = 0;
uint32 ke_MQ = 0;
uint32 ke_SC = 0;
uint32 ke_SR = 0;

DEVICE ke_dev;
t_stat ke_rd (int32 *data, int32 PA, int32 access);
t_stat ke_wr (int32 data, int32 PA, int32 access);
t_stat ke_reset (DEVICE *dptr);
uint32 ke_set_SR (void);
t_stat ke_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *ke_description (DEVICE *dptr);

#define IOLN_KE         020

DIB ke_dib = { IOBA_AUTO, IOLN_KE, &ke_rd, &ke_wr, 0 };

UNIT ke_unit = {
    UDATA (NULL, UNIT_DISABLE, 0)
    };

REG ke_reg[] = {
    { ORDATAD (AC, ke_AC, 16, "accumulator") },
    { ORDATAD (MQ, ke_MQ, 16, "multiplier-quotient") },
    { ORDATAD (SC, ke_SC,  6, "shift count") },
    { ORDATAD (SR, ke_SR,  8, "status register") },
    { NULL }
    };

MTAB ke_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 010, "ADDRESS", NULL,
        NULL, &show_addr, NULL, "Bus address" },
    { 0 }
    };

DEVICE ke_dev = {
    "KE", &ke_unit, ke_reg, ke_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ke_reset,
    NULL, NULL, NULL,
    &ke_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS, 0,
    NULL, NULL, NULL, &ke_help, NULL, NULL,
    &ke_description 
    };

/* KE read - reads are always 16b, to even addresses */

t_stat ke_rd (int32 *data, int32 PA, int32 access)
{  
switch (PA & 016) {                                     /* decode PA<3:1> */

    case KE_AC:                                         /* AC */
        *data = ke_AC;
        break;

    case KE_MQ:                                         /* MQ */
        *data = ke_MQ;
        break;

    case KE_NOR:                                        /* norm (SC) */
        *data = ke_SC;
        break;

    case KE_SC:                                         /* SR/SC */
        *data = (ke_set_SR () << 8) | ke_SC;
        break;

    default:
        *data = 0;
        break;
        }

return SCPE_OK;
}

/* KE write - writes trigger actual arithmetic */

t_stat ke_wr (int32 data, int32 PA, int32 access)
{
int32 quo, t32, sout, sign;
uint32 absd, absr;

switch (PA & 017) {                                     /* decode PA<3:0> */

    case KE_DIV:                                        /* divide */
        if ((access == WRITEB) && GET_SIGN_B (data))    /* byte write? */
            data |= 0177400;                            /* sext data to 16b */
        ke_SR = 0;                                      /* N = V = C = 0 */
        t32 = (ke_AC << 16) | ke_MQ;                    /* 32b divd */
        if (GET_SIGN_W (ke_AC))                         /* sext (divd) */
            t32 = t32 | ~017777777777;
        if (GET_SIGN_W (data))                          /* sext (divr) */
            data = data | ~077777;
        absd = abs (t32);
        absr = abs (data);
        if ((absd >> 16) >= absr) {                     /* divide fails? */

/* Based on the documentation, here's what has happened:

   SC = 16.
   SR<c> = (AC<15> == data<15>)
   AC'MQ = (AC'MQ << 1) | SR<c>
   AC = SR<c>? AC - data: AC + data
   SR<c> = (AC<15> == data<15>)
   SC = SC - 1
   stop
*/

            sign = GET_SIGN_W (ke_AC ^ data) ^ 1;       /* 1 if signs match */
            ke_AC = (ke_AC << 1) | (ke_MQ >> 15);
            ke_AC = (sign? ke_AC - data: ke_AC + data) & DMASK;
            ke_MQ = ((ke_MQ << 1) | sign) & DMASK;
            if (GET_SIGN_W (ke_AC ^ data) == 0)         /* 0 if signs match */
                ke_SR |= KE_SR_C;            
            ke_SC = 15;                                 /* SC clocked once */
            ke_SR |= KE_SR_NXV;                         /* set overflow */
           }
        else {
            ke_SC = 0;
            quo = t32 / data;
            ke_MQ = quo & DMASK;                        /* MQ has quo */
            ke_AC = (t32 % data) & DMASK;               /* AC has rem */
            if ((quo > 32767) || (quo < -32768))        /* quo overflow? */
                ke_SR |= KE_SR_NXV;                     /* set overflow */
            }
        if (GET_SIGN_W (ke_MQ))                         /* result negative? */
            ke_SR ^= (KE_SR_N | KE_SR_NXV);             /* N = 1, compl NXV */
        break;

    case KE_AC:                                         /* AC */
        if ((access == WRITEB) && GET_SIGN_B (data))    /* byte write? */
            data |= 0177400;                            /* sext data to 16b */
        ke_AC = data;
        break;

    case KE_AC + 1:                                     /* AC odd byte */
        ke_AC = (ke_AC & 0377) | (data << 8);
        break;

    case KE_MQ:                                         /* MQ */
        if ((access == WRITEB) && GET_SIGN_B (data))    /* byte write? */
            data |= 0177400;                            /* sext data to 16b */
        ke_MQ = data;
        if (GET_SIGN_W (ke_MQ))                         /* sext MQ to AC */
            ke_AC = 0177777;
        else ke_AC = 0;
        break;

    case KE_MQ + 1:                                     /* MQ odd byte */
        ke_MQ = (ke_MQ & 0377) | (data << 8);
        if (GET_SIGN_W (ke_MQ))                         /* sext MQ to AC */
            ke_AC = 0177777;
        else ke_AC = 0;
        break;

    case KE_MUL:                                        /* multiply */
        if ((access == WRITEB) && GET_SIGN_B (data))    /* byte write? */
            data |= 0177400;                            /* sext data to 16b */
        ke_SC = 0;
        if (GET_SIGN_W (data))                          /* sext operands */
            data |= ~077777;
        t32 = ke_MQ;
        if (GET_SIGN_W (t32))
            t32 |= ~077777;
        t32 = t32 * data;
        ke_AC = (t32 >> 16) & DMASK;
        ke_MQ = t32 & DMASK;
        if (GET_SIGN_W (ke_AC))                         /* result negative? */
            ke_SR = KE_SR_N | KE_SR_NXV;                /* N = 1, V = C = 0 */
        else ke_SR = 0;                                 /* N = 0, V = C = 0 */
        break;

    case KE_SC:                                         /* SC */
        if (access == WRITEB)                           /* ignore byte writes */
            return SCPE_OK;
        ke_SR = (data >> 8) & (KE_SR_NXV|KE_SR_N|KE_SR_C);
        ke_SC = data & 077;
        break;

    case KE_NOR:                                        /* normalize */
        for (ke_SC = 0; ke_SC < 31; ke_SC++) {          /* max 31 shifts */
            if (((ke_AC == 0140000) && (ke_MQ == 0)) || /* special case? */
                (GET_SIGN_W (ke_AC ^ (ke_AC << 1))))    /* AC<15> != AC<14>? */
                break;
            ke_AC = ((ke_AC << 1) | (ke_MQ >> 15)) & DMASK;
            ke_MQ = (ke_MQ << 1) & DMASK;
            }
        if (GET_SIGN_W (ke_AC))                         /* result negative? */
            ke_SR = KE_SR_N | KE_SR_NXV;                /* N = 1, V = C = 0 */
        else ke_SR = 0;                                 /* N = 0, V = C = 0 */
        break;

    case KE_LSH:                                        /* logical shift */
        ke_SC = 0;
        ke_SR = 0;                                      /* N = V = C = 0 */
        data = data & 077;                              /* 6b shift count */
        if (data != 0) {
            t32 = (ke_AC << 16) | ke_MQ;                /* 32b operand */
            if ((sign = GET_SIGN_W (ke_AC)))            /* sext operand */
                t32 = t32 | ~017777777777;
            if (data < 32) {                            /* [1,31] - left */
                sout = (t32 >> (32 - data)) | (-sign << data);
                t32 = ((uint32) t32) << data;           /* do shift (zext) */
                if (sout != (GET_SIGN_L (t32)? -1: 0))  /* bits lost = sext? */
                    ke_SR |= KE_SR_NXV;                 /* no, V = 1 */
                if (sout & 1)                           /* last bit lost = 1? */
                    ke_SR |= KE_SR_C;                   /* yes, C = 1 */
                }
            else {                                      /* [32,63] = -32,-1 */
                if ((t32 >> (63 - data)) & 1)           /* last bit lost = 1? */
                    ke_SR |= KE_SR_C;                   /* yes, C = 1*/
                t32 = (data != 32)? ((uint32) t32) >> (64 - data): 0;
                }
           ke_AC = (t32 >> 16) & DMASK;
           ke_MQ = t32 & DMASK;
           }
       if (GET_SIGN_W (ke_AC))                          /* result negative? */
           ke_SR ^= (KE_SR_N | KE_SR_NXV);              /* N = 1, compl NXV */
       break;

/* EAE ASH differs from EIS ASH and cannot use the same overflow test */

    case KE_ASH:                                        /* arithmetic shift */
        ke_SC = 0;
        ke_SR = 0;                                      /* N = V = C = 0 */
        data = data & 077;                              /* 6b shift count */
        if (data != 0) {
            t32 = (ke_AC << 16) | ke_MQ;                /* 32b operand */
            if ((sign = GET_SIGN_W (ke_AC)))            /* sext operand */
                t32 = t32 | ~017777777777;
            if (data < 32) {                            /* [1,31] - left */
                sout = (t32 >> (31 - data)) | (-sign << data);
                t32 = (t32 & 020000000000) | ((t32 << data) & 017777777777);
                if (sout != (GET_SIGN_L (t32)? -1: 0))  /* bits lost = sext? */
                    ke_SR |= KE_SR_NXV;                 /* no, V = 1 */
                if (sout & 1)                           /* last bit lost = 1? */
                    ke_SR |= KE_SR_C;                   /* yes, C = 1 */
                }
            else {                                      /* [32,63] = -32,-1 */
                if ((t32 >> (63 - data)) & 1)           /* last bit lost = 1? */
                    ke_SR |= KE_SR_C;                   /* yes, C = 1 */
                t32 = (data != 32)?                     /* special case 32 */
                    (((uint32) t32) >> (64 - data)) | (-sign << (data - 32)):
                    -sign;
                }
           ke_AC = (t32 >> 16) & DMASK;
           ke_MQ = t32 & DMASK;
           }
       if (GET_SIGN_W (ke_AC))                          /* result negative? */
           ke_SR ^= (KE_SR_N | KE_SR_NXV);              /* N = 1, compl NXV */
       break;

    default:                                            /* all others ignored */
       return SCPE_OK;
    }                                                   /* end switch PA */

ke_set_SR ();
return SCPE_OK;
}

/* Update status register based on current AC, MQ */

uint32 ke_set_SR (void)
{
ke_SR &= ~KE_SR_DYN;                                    /* clr dynamic bits */
if (ke_MQ == 0)                                         /* MQ == 0? */
    ke_SR |= KE_SR_MQZ;
if (ke_AC == 0) {                                       /* AC == 0? */
    ke_SR |= KE_SR_ACZ;
    if (GET_SIGN_W (ke_MQ) == 0)                        /* MQ positive? */
        ke_SR |= KE_SR_SXT;
    if (ke_MQ == 0)                                     /* MQ zero? */
        ke_SR |= KE_SR_Z;
    }
if (ke_AC == 0177777) {                                 /* AC == 177777? */
    ke_SR |= KE_SR_ACM1;
    if (GET_SIGN_W (ke_MQ) == 1)                        /* MQ negative? */
        ke_SR |= KE_SR_SXT;
    }
return ke_SR;
}

/* Reset routine */

t_stat ke_reset (DEVICE *dptr)
{
ke_SR = 0;
ke_SC = 0;
ke_AC = 0;
ke_MQ = 0;
return auto_config(0, 0);
}

t_stat ke_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
const char *const text =
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"KE11A Extended Arithmetic Option (KE)\n"
"\n"
" The KE11A extended arithmetic option (KE) provides multiply, divide,\n"
" normalization, and multi-bit shift capability on Unibus PDP-11's that\n"
" lack the EIS instruction set.\n"
"\n"
" The KE11-A performs five arithmetic operations.\n"
"   a. Multiplication\n"
"   b. Division\n"
"   c. Three different shift operations on data operands of up to 32 bits.\n"
"\n"
" In practice, it was only sold with the PDP-11/20.\n"
" The KE is disabled by default.\n";
fprintf (st, "%s", text);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

char *ke_description (DEVICE *dptr)
{
return "KE11-A extended arithmetic element";
}
