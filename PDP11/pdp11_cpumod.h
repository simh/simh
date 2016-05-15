/* pdp11_cpumod.h: PDP-11 CPU model definitions

   Copyright (c) 2004-2015, Robert M Supnik

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
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   30-Dec-15    RMS     Added 11/03, 11/23 BEVENT disable
   22-Apr-08    RMS     Added 11/70 MBRK register
   30-Aug-05    RMS     Added additional 11/60 registers
*/

#ifndef PDP11_CPUMOD_H_
#define PDP11_CPUMOD_H_        0

#define SOP_1103        (BUS_Q|OPT_BVT)
#define OPT_1103        (OPT_EIS|OPT_FIS|OPT_BVT)
#define PSW_1103        0000377

#define SOP_1104        (BUS_U)
#define OPT_1104        0
#define PSW_1104        0000377

#define SOP_1105        (BUS_U)
#define OPT_1105        0
#define PSW_1105        0000377

#define SOP_1120        (BUS_U)
#define OPT_1120        0
#define PSW_1120        0000377

#define SOP_1123        (BUS_Q|OPT_EIS|OPT_FPP|OPT_MMU|OPT_BVT)
#define OPT_1123        (OPT_FPP|OPT_CIS|OPT_BVT)
#define PSW_F           0170777
#define PAR_F           0177777
#define PDR_F           0077516
#define MM0_F           0160157
#define MM3_F           0000060

#define SOP_1123P       (BUS_Q|OPT_EIS|OPT_FPP|OPT_MMU)
#define OPT_1123P       (OPT_FPP|OPT_CIS)

#define SOP_1124        (BUS_U|OPT_EIS|OPT_FPP|OPT_MMU|OPT_UBM)
#define OPT_1124        (OPT_FPP|OPT_CIS)

#define SOP_1134        (BUS_U|OPT_EIS|OPT_MMU)
#define OPT_1134        (OPT_FPP)
#define PSW_1134        0170377
#define PAR_1134        0007777
#define PDR_1134        0077516
#define MM0_1134        0160557

#define SOP_1140        (BUS_U|OPT_EIS|OPT_MMU)
#define OPT_1140        (OPT_FIS)
#define PSW_1140        0170377
#define PAR_1140        0007777
#define PDR_1140        0077516
#define MM0_1140        0160557

#define SOP_1144        (BUS_U|OPT_EIS|OPT_FPP|OPT_MMU|OPT_UBM)
#define OPT_1144        (OPT_FPP|OPT_CIS)
#define PSW_1144        0170777
#define PAR_1144        0177777
#define PDR_1144        0177516
#define MM0_1144        0160557
#define MM3_1144        0000077

#define SOP_1145        (BUS_U|OPT_EIS|OPT_FPP|OPT_MMU|OPT_RH11)
#define OPT_1145        (OPT_FPP)
#define PSW_1145        0174377
#define PAR_1145        0007777
#define PDR_1145        0077717
#define MM0_1145        0171777
#define MM3_1145        0000007

#define SOP_1160        (BUS_U|OPT_EIS|OPT_FPP|OPT_MMU)
#define OPT_1160        0
#define PSW_1160        0170377
#define PAR_1160        0007777
#define PDR_1160        0077516
#define MM0_1160        0160557

#define SOP_1170        (BUS_U|OPT_EIS|OPT_FPP|OPT_MMU|OPT_UBM)
#define OPT_1170        (OPT_FPP|OPT_RH11)
#define PSW_1170        0174377
#define PAR_1170        0177777
#define PDR_1170        0077717
#define MM0_1170        0171777
#define MM3_1170        0000067

#define SOP_1173        (BUS_Q|OPT_EIS|OPT_FPP|OPT_MMU)
#define OPT_1173        (OPT_CIS)
#define PSW_J           0174777
#define PAR_J           0177777
#define PDR_J           0177516
#define MM0_J           0160177
#define MM3_J           0000077

#define SOP_1153        (BUS_Q|OPT_EIS|OPT_FPP|OPT_MMU)
#define OPT_1153        (OPT_CIS)

#define SOP_1173B       (BUS_Q|OPT_EIS|OPT_FPP|OPT_MMU)
#define OPT_1173B       (OPT_CIS)

#define SOP_1183        (BUS_Q|OPT_EIS|OPT_FPP|OPT_MMU)
#define OPT_1183        (OPT_CIS)

#define SOP_1184        (BUS_U|OPT_EIS|OPT_FPP|OPT_MMU|OPT_UBM|OPT_RH11)
#define OPT_1184        (OPT_CIS)

#define SOP_1193        (BUS_Q|OPT_EIS|OPT_FPP|OPT_MMU)
#define OPT_1193        (OPT_CIS)

#define SOP_1194        (BUS_U|OPT_EIS|OPT_FPP|OPT_MMU|OPT_UBM|OPT_RH11)
#define OPT_1194        (OPT_CIS)

#define MOD_MAX         20

/* MFPT codes */

#define MFPT_44         1
#define MFPT_F          3
#define MFPT_T          4
#define MFPT_J          5

/* KDF11B specific register */

#define PCRFB_RW        0037477                         /* page ctrl reg */

#define CDRFB_RD        0000377                         /* config reg */
#define CDRFB_WR        0000017

/* KT24 Unibus map specific registers */

#define LMAL_RD         0177777                         /* last mapped low */

#define LMAH_RD         0000177                         /* last mapped high */
#define LMAH_WR         0000100

/* 11/44 specific registers */

#define CCR44_RD        0033315                         /* cache control */
#define CCR44_WR        0003315

#define CMR44_RD        0177437                         /* cache maint */
#define CMR44_WR        0000037

#define CPUE44_BUSE     0004000

/* 11/60 specific registers */

#define WCS60_RD        0161776                         /* WCS control */
#define WCS60_WR        0061676

#define MEME60_RD       0100340                         /* memory error */

#define CCR60_RD        0000315                         /* cache control */
#define CCR60_WR        0000115

#define MBRK60_WR       0007777                         /* microbreak */

#define CPUE60_RD       (CPUE_ODD|CPUE_TMO|CPUE_RED)

/* 11/70 specific registers */

#define MBRK70_WR       0000377                         /* microbreak */

/* J11 specific registers */

/* Maintenance register */

#define MAINT_V_UQ      9                               /* Q/U flag */
#define MAINT_Q         (0 << MAINT_V_UQ)               /* Qbus */
#define MAINT_U         (1 << MAINT_V_UQ)
#define MAINT_V_FPA     8                               /* FPA flag */
#define MAINT_NOFPA     (0 << MAINT_V_FPA)
#define MAINT_FPA       (1 << MAINT_V_FPA)
#define MAINT_V_TYP     4                               /* system type */
#define MAINT_KDJA      (1 << MAINT_V_TYP)              /* KDJ11A */
#define MAINT_KDJB      (2 << MAINT_V_TYP)              /* KDJ11B */
#define MAINT_KDJD      (4 << MAINT_V_TYP)              /* KDJ11D */
#define MAINT_KDJE      (5 << MAINT_V_TYP)              /* KDJ11E */
#define MAINT_V_HTRAP   3                               /* trap 4 on HALT */
#define MAINT_HTRAP     (1 << MAINT_V_HTRAP)
#define MAINT_V_POM     1                               /* power on option */
#define MAINT_POODT     (0 << MAINT_V_POM)              /* power up ODT */
#define MAINT_POROM     (2 << MAINT_V_POM)              /* power up ROM */
#define MAINT_V_BPOK    0                               /* power OK */
#define MAINT_BPOK      (1 << MAINT_V_BPOK)

/* KDJ11B control */

#define CSRJB_RD        0177767
#define CSRJB_WR        0037767
#define CSRJ_LTCI       0020000                         /* force LTC int */
#define CSRJ_LTCD       0010000                         /* disable LTC reg */
#define CSRJ_V_LTCSEL   10
#define CSRJ_M_LTCSEL   03
#define CSRJ_LTCSEL(x)  (((x) >> CSRJ_V_LTCSEL) & CSRJ_M_LTCSEL)
#define CSRJ_HBREAK     0001000                         /* halt on break */

#define PCRJB_RW        0077176                         /* page ctrl reg */

#define CDRJB_RD        0000377                         /* config register */
#define CDRJB_WR        0000377

/* KDJ11D control */

#define CSRJD_RD        0157777                         /* native register */
#define CSRJD_WR        0000377
#define CSRJD_15M       0040000                         /* 1.5M mem on board */

/* KDJ11E control */

#define CSRJE_RD        0137360                         /* control reg */
#define CSRJE_WR        0037370

#define PCRJE_RW        0177376                         /* page ctrl reg */

#define CDRJE_RD        0000377                         /* config register */
#define CDRJE_WR        0000077

#define ASRJE_RW        0030462                         /* additional status */
#define ASRJE_V_TOY     8
#define ASRJE_TOY       (1u << ASRJE_V_TOY)             /* TOY serial bit */
#define ASRJE_TOYBIT(x) (((x) >> ASRJE_V_TOY) & 1)

/* KDJ11E TOY clock */

#define TOY_HSEC        0
#define TOY_SEC         1
#define TOY_MIN         2
#define TOY_HR          3
#define TOY_DOW         4
#define TOY_DOM         5
#define TOY_MON         6
#define TOY_YR          7
#define TOY_LNT         8

/* KTJ11B Unibus map */

#define DCRKTJ_RD       0100616                         /* diag control */
#define DCRKTJ_WR       0000416

#define DDRKTJ_RW       0177777                         /* diag data */

#define MCRKTJ_RD       0000377                         /* control register */
#define MCRKTJ_WR       0000177

/* Data tables */

struct cpu_table {
    const char          *name;                          /* model name */
    uint32              std;                            /* standard flags */
    uint32              opt;                            /* set/clear flags */
    uint32              maxm;                           /* max memory */
    uint32              psw;                            /* PSW mask */
    uint32              mfpt;                           /* MFPT code */
    uint32              par;                            /* PAR mask */
    uint32              pdr;                            /* PDR mask */
    uint32              mm0;                            /* MMR0 mask */
    uint32              mm3;                            /* MMR3 mask */
    };

typedef struct cpu_table CPUTAB;

struct conf_table {
    uint32              cpum;
    uint32              optm;
    DIB                 *dib;
    };

typedef struct conf_table CNFTAB;

/* Prototypes */

t_stat cpu_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_clr_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_bus (int32 opt);

#endif
