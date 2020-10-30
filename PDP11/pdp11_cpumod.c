/* pdp11_cpumod.c: PDP-11 CPU model-specific features

   Copyright (c) 2004-2020, Robert M Supnik

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

   system       PDP-11 model-specific registers

   15-Sep-20    RMS     Fixed problem in KDJ11E programmable clock (Paul Koning)
   04-Mar-16    RMS     Fixed maximum memory sizes to exclude IO page
   14-Mar-16    RMS     Modified to keep cpu_memsize in sync with MEMSIZE
   06-Jun-13    RMS     Fixed change model to set memory size last
   20-May-08    RMS     Added JCSR default for KDJ11B, KDJ11E
   22-Apr-08    RMS     Fixed write behavior of 11/70 MBRK, LOSIZE, HISIZE
                        (Walter Mueller)
   29-Apr-07    RMS     Don't run bus setup routine during RESTORE
   30-Aug-05    RMS     Added additional 11/60 registers
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   15-Feb-05    RMS     Fixed bug in SHOW MODEL (Sergey Okhapkin)
   19-Jan-05    RMS     Added variable SYSID, MBRK write (Tim Chapman)

   This module includes CPU- and system-specific registers, such as the Unibus
   map and control registers on 22b Unibus systems, the board registers for the
   F11- and J11-based systems, and the system registers for the PDP-11/44,
   PDP-11/45, PDP-11/60, and PDP-11/70.  Most registers are implemented at
   a minimum level: just enough to satisfy the machine identification code
   in the various operating systems.
*/

#include "pdp11_defs.h"
#include "pdp11_cpumod.h"

/* Byte write macros for system registers */

#define ODD_IGN(cur) \
    if ((access == WRITEB) && (pa & 1)) \
        return SCPE_OK
#define ODD_WO(cur) \
    if ((access == WRITEB) && (pa & 1)) \
        cur = cur << 8
#define ODD_MRG(prv,cur) \
    if (access == WRITEB) \
        cur =((pa & 1)? (((prv) & 0377) | ((cur) & 0177400)) : \
                        (((prv) & 0177400) | ((cur) & 0377)))

int32 SR = 0;                                           /* switch register */
int32 DR = 0;                                           /* display register */
int32 MBRK = 0;                                         /* 11/70 microbreak */
int32 SYSID = 0x1234;                                   /* 11/70 system ID */
int32 WCS = 0;                                          /* 11/60 WCS control */
int32 CPUERR = 0;                                       /* CPU error reg */
int32 MEMERR = 0;                                       /* memory error reg */
int32 CCR = 0;                                          /* cache control reg */
int32 HITMISS = 0;                                      /* hit/miss reg */
int32 MAINT = 0;                                        /* maint reg */
int32 JCSR = 0;                                         /* J11 control */
int32 JCSR_dflt = 0;                                    /* J11 boot ctl def */
int32 JPCR = 0;                                         /* J11 page ctrl */
int32 JASR = 0;                                         /* J11 addtl status */
int32 UDCR = 0;                                         /* UBA diag ctrl */
int32 UDDR = 0;                                         /* UBA diag data */
int32 UCSR = 0;                                         /* UBA control */
int32 uba_last = 0;                                     /* UBA last mapped */
int32 ub_map[UBM_LNT_LW] = { 0 };                       /* UBA map array */
int32 toy_state = 0;
uint8 toy_data[TOY_LNT] = { 0 };
static int32 clk_tps_map[4] = { 0, 50, 60, 800 };       /* 0 = use BEVENT */

extern int32 R[8];
extern int32 STKLIM, PIRQ;
extern int32 clk_fie, clk_fnxm, clk_tps, clk_default;

t_stat CPU24_rd (int32 *data, int32 addr, int32 access);
t_stat CPU24_wr (int32 data, int32 addr, int32 access);
t_stat CPU44_rd (int32 *data, int32 addr, int32 access);
t_stat CPU44_wr (int32 data, int32 addr, int32 access);
t_stat CPU45_rd (int32 *data, int32 addr, int32 access);
t_stat CPU45_wr (int32 data, int32 addr, int32 access);
t_stat CPU60_rd (int32 *data, int32 addr, int32 access);
t_stat CPU60_wr (int32 data, int32 addr, int32 access);
t_stat CPU70_rd (int32 *data, int32 addr, int32 access);
t_stat CPU70_wr (int32 data, int32 addr, int32 access);
t_stat CPUJ_rd (int32 *data, int32 addr, int32 access);
t_stat CPUJ_wr (int32 data, int32 addr, int32 access);
t_stat REG_rd (int32 *data, int32 addr, int32 access);
t_stat REG_wr (int32 data, int32 addr, int32 access);
t_stat SR_rd (int32 *data, int32 addr, int32 access);
t_stat DR_wr (int32 data, int32 addr, int32 access);
t_stat CTLFB_rd (int32 *data, int32 addr, int32 access);
t_stat CTLFB_wr (int32 data, int32 addr, int32 access);
t_stat CTLJB_rd (int32 *data, int32 addr, int32 access);
t_stat CTLJB_wr (int32 data, int32 addr, int32 access);
t_stat CTLJD_rd (int32 *data, int32 addr, int32 access);
t_stat CTLJD_wr (int32 data, int32 addr, int32 access);
t_stat CTLJE_rd (int32 *data, int32 addr, int32 access);
t_stat CTLJE_wr (int32 data, int32 addr, int32 access);
t_stat UBA24_rd (int32 *data, int32 addr, int32 access);
t_stat UBA24_wr (int32 data, int32 addr, int32 access);
t_stat UBAJ_rd (int32 *data, int32 addr, int32 access);
t_stat UBAJ_wr (int32 data, int32 addr, int32 access);
t_stat sys_reset (DEVICE *dptr);
int32 toy_read (void);
void toy_write (int32 bit);
uint8 toy_set (int32 val);
t_stat sys_set_jclk_dflt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sys_show_jclk_dflt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

extern t_stat PSW_rd (int32 *data, int32 addr, int32 access);
extern t_stat PSW_wr (int32 data, int32 addr, int32 access);
extern t_stat APR_rd (int32 *data, int32 addr, int32 access);
extern t_stat APR_wr (int32 data, int32 addr, int32 access);
extern t_stat MMR012_rd (int32 *data, int32 addr, int32 access);
extern t_stat MMR012_wr (int32 data, int32 addr, int32 access);
extern t_stat MMR3_rd (int32 *data, int32 addr, int32 access);
extern t_stat MMR3_wr (int32 data, int32 addr, int32 access);
extern t_stat ubm_rd (int32 *data, int32 addr, int32 access);
extern t_stat ubm_wr (int32 data, int32 addr, int32 access);
extern void put_PIRQ (int32 val);

/* Fixed I/O address table entries */

DIB psw_dib = { IOBA_PSW, IOLN_PSW, &PSW_rd, &PSW_wr, 0 };
DIB cpuj_dib = { IOBA_CPU, IOLN_CPU, &CPUJ_rd, &CPUJ_wr, 0 };
DIB cpu24_dib = { IOBA_CPU, IOLN_CPU, &CPU24_rd, &CPU24_wr, 0 };
DIB cpu44_dib = { IOBA_CPU, IOLN_CPU, &CPU44_rd, &CPU44_wr, 0 };
DIB cpu45_dib = { IOBA_CPU, IOLN_CPU, &CPU45_rd, &CPU45_wr, 0 };
DIB cpu60_dib = { IOBA_CPU, IOLN_CPU, &CPU60_rd, &CPU60_wr, 0 };
DIB cpu70_dib = { IOBA_CPU, IOLN_CPU, &CPU70_rd, &CPU70_wr, 0 };
DIB reg_dib = { IOBA_GPR, IOLN_GPR, &REG_rd, &REG_wr, 0 };
DIB ctlfb_dib = { IOBA_CTL, IOLN_CTL, &CTLFB_rd, &CTLFB_wr };
DIB ctljb_dib = { IOBA_CTL, IOLN_CTL, &CTLJB_rd, &CTLJB_wr };
DIB ctljd_dib = { IOBA_CTL, IOLN_CTL, &CTLJD_rd, &CTLJD_wr };
DIB ctlje_dib = { IOBA_CTL, IOLN_CTL, &CTLJE_rd, &CTLJE_wr };
DIB uba24_dib = { IOBA_UCTL, IOLN_UCTL, &UBA24_rd, &UBA24_wr };
DIB ubaj_dib = {IOBA_UCTL, IOLN_UCTL, &UBAJ_rd, &UBAJ_wr };
DIB supv_dib = { IOBA_SUP, IOLN_SUP, &APR_rd, &APR_wr, 0 };
DIB kipdr_dib = { IOBA_KIPDR, IOLN_KIPDR, &APR_rd, &APR_wr, 0 };
DIB kdpdr_dib = { IOBA_KDPDR, IOLN_KDPDR, &APR_rd, &APR_wr, 0 };
DIB kipar_dib = { IOBA_KIPAR, IOLN_KIPAR, &APR_rd, &APR_wr, 0 };
DIB kdpar_dib = { IOBA_KDPAR, IOLN_KDPAR, &APR_rd, &APR_wr, 0 };
DIB uipdr_dib = { IOBA_UIPDR, IOLN_UIPDR, &APR_rd, &APR_wr, 0 };
DIB udpdr_dib = { IOBA_UDPDR, IOLN_UDPDR, &APR_rd, &APR_wr, 0 };
DIB uipar_dib = { IOBA_UIPAR, IOLN_UIPAR, &APR_rd, &APR_wr, 0 };
DIB udpar_dib = { IOBA_UDPAR, IOLN_UDPAR, &APR_rd, &APR_wr, 0 };
DIB sr_dib = { IOBA_SR, IOLN_SR, &SR_rd, NULL, 0 };
DIB dr_dib = { IOBA_SR, IOLN_SR, NULL, &DR_wr, 0 };
DIB mmr012_dib = { IOBA_MMR012, IOLN_MMR012, &MMR012_rd, &MMR012_wr, 0 };
DIB mmr3_dib = { IOBA_MMR3, IOLN_MMR3, &MMR3_rd, &MMR3_wr, 0 };
DIB ubm_dib = { IOBA_UBM, IOLN_UBM, &ubm_rd, &ubm_wr, 0 };

CPUTAB cpu_tab[MOD_MAX] = {
    { "11/03", SOP_1103, OPT_1103, MEMSIZE64K, PSW_1103,
       0, 0, 0, 0, 0 },
    { "11/04", SOP_1104, OPT_1104, MEMSIZE64K, PSW_1104,
       0, 0, 0, 0, 0 },
    { "11/05", SOP_1105, OPT_1105, MEMSIZE64K, PSW_1105,
       0, 0, 0, 0, 0 },
    { "11/20", SOP_1120, OPT_1120, MEMSIZE64K, PSW_1120,
       0, 0, 0, 0, 0 },
    { "11/23", SOP_1123, OPT_1123, MAXMEMSIZE, PSW_F,
       MFPT_F, PAR_F, PDR_F, MM0_F, MM3_F },
    { "11/23+", SOP_1123P, OPT_1123P, MAXMEMSIZE, PSW_F,
       MFPT_F, PAR_F, PDR_F, MM0_F, MM3_F },
    { "11/24", SOP_1124, OPT_1124, MAXMEMSIZE, PSW_F,
       MFPT_F, PAR_F, PDR_F, MM0_F, MM3_F },
    { "11/34", SOP_1134, OPT_1134, UNIMEMSIZE, PSW_1134,
       0, PAR_1134, PDR_1134, MM0_1134, 0 },
    { "11/40", SOP_1140, OPT_1140, UNIMEMSIZE, PSW_1140,
       0, PAR_1140, PDR_1140, MM0_1140, 0 },
    { "11/44", SOP_1144, OPT_1144, MAXMEMSIZE, PSW_1144,
       MFPT_44, PAR_1144, PDR_1144, MM0_1144, MM3_1144 },
    { "11/45", SOP_1145, OPT_1145, UNIMEMSIZE, PSW_1145,
       0, PAR_1145, PDR_1145, MM0_1145, MM3_1145 },
    { "11/60", SOP_1160, OPT_1160, UNIMEMSIZE, PSW_1160,
       0, PAR_1160, PDR_1160, MM0_1160, 0 },
    { "11/70", SOP_1170, OPT_1170, MAXMEMSIZE, PSW_1170,
       0, PAR_1170, PDR_1170, MM0_1170, MM3_1170 },
    { "11/73", SOP_1173, OPT_1173, MAXMEMSIZE, PSW_J,
       MFPT_J, PAR_J, PDR_J, MM0_J, MM3_J },
    { "11/53", SOP_1153, OPT_1153, MAXMEMSIZE, PSW_J,
       MFPT_J, PAR_J, PDR_J, MM0_J, MM3_J },
    { "11/73B", SOP_1173B, OPT_1173B, MAXMEMSIZE, PSW_J,
       MFPT_J, PAR_J, PDR_J, MM0_J, MM3_J },
    { "11/83", SOP_1183, OPT_1183, MAXMEMSIZE, PSW_J,
       MFPT_J, PAR_J, PDR_J, MM0_J, MM3_J },
    { "11/84", SOP_1184, OPT_1184, MAXMEMSIZE, PSW_J,
       MFPT_J, PAR_J, PDR_J, MM0_J, MM3_J },
    { "11/93", SOP_1193, OPT_1193, MAXMEMSIZE, PSW_J,
       MFPT_J, PAR_J, PDR_J, MM0_J, MM3_J },
    { "11/94", SOP_1194, OPT_1194, MAXMEMSIZE, PSW_J,
       MFPT_J, PAR_J, PDR_J, MM0_J, MM3_J }
    };

CNFTAB cnf_tab[] = {
    { HAS_PSW,  0, &psw_dib },                          /* PSW */
    { CPUT_J,   0, &cpuj_dib },                         /* CPU control */
    { CPUT_24,  0, &cpu24_dib },
    { CPUT_44,  0, &cpu44_dib },
    { CPUT_45,  0, &cpu45_dib },
    { CPUT_60,  0, &cpu60_dib },
    { CPUT_70,  0, &cpu70_dib },
    { HAS_IOSR, 0, &reg_dib },
    { CPUT_23P, 0, &ctlfb_dib },                        /* board ctls */
    { CPUT_JB,  0, &ctljb_dib },
    { CPUT_53,  0, &ctljd_dib },
    { CPUT_JE,  0, &ctlje_dib },
    { CPUT_24,  0, &uba24_dib },                        /* UBA */
    { CPUT_JU,  0, &ubaj_dib },
    { 0, OPT_MMU,  &kipdr_dib },                        /* MMU */
    { 0, OPT_MMU,  &kipar_dib },
    { 0, OPT_MMU,  &uipdr_dib },
    { 0, OPT_MMU,  &uipar_dib },
    { 0, OPT_MMU,  &mmr012_dib },                       /* MMR0-2 */
    { HAS_MMR3, 0, &mmr3_dib },                         /* MMR3 */
    { 0, OPT_UBM,  &ubm_dib },                          /* Unibus map */
    { HAS_SID,  0, &kdpdr_dib },                        /* supv, I/D */
    { HAS_SID,  0, &kdpar_dib },
    { HAS_SID,  0, &supv_dib },
    { HAS_SID,  0, &udpdr_dib },
    { HAS_SID,  0, &udpar_dib },
    { HAS_SR,   0, &sr_dib },                           /* SR */
    { HAS_DR,   0, &dr_dib },                           /* DR */
    { 0, 0,        NULL }
    };

static const char *opt_name[] = {
    "Unibus", "Qbus", "EIS", "NOEIS", "FIS", "NOFIS",
    "FPP", "NOFPP", "CIS", "NOCIS", "MMU", "NOMMU",
    "RH11", "RH70", "PARITY", "NOPARITY", "Unibus map", "No map", 
    "BEVENT enabled", "BEVENT disabled", NULL
    };

static const char *jcsr_val[4] = {
    "LINE", "50HZ", "60HZ", "800HZ"
    };

/* SYSTEM data structures

   sys_dev      SYSTEM device descriptor
   sys_unit     SYSTEM unit descriptor
   sys_reg      SYSTEM register list
*/

UNIT sys_unit = { UDATA (NULL, 0, 0) };

REG sys_reg[] = {
    { ORDATA (SR, SR, 16) },
    { ORDATA (DR, DR, 16) },
    { ORDATA (MEMERR, MEMERR, 16) },
    { ORDATA (CCR, CCR, 16) },
    { ORDATA (MAINT, MAINT, 16) },
    { ORDATA (HITMISS, HITMISS, 16) },
    { ORDATA (CPUERR, CPUERR, 16) },
    { ORDATA (MBRK, MBRK, 16) },
    { ORDATA (WCS, WCS, 16) },
    { ORDATA (SYSID, SYSID, 16) },
    { ORDATA (JCSR, JCSR, 16) },
    { ORDATA (JCSR_DFLT, JCSR_dflt, 16), REG_HRO },
    { ORDATA (JPCR, JPCR, 16) },
    { ORDATA (JASR, JASR, 16) },
    { ORDATA (UDCR, UDCR, 16) },
    { ORDATA (UDDR, UDDR, 16) },
    { ORDATA (UCSR, UCSR, 16) },
    { ORDATA (ULAST, uba_last, 23) },
    { BRDATA (UBMAP, ub_map, 8, 22, UBM_LNT_LW) },
    { DRDATA (TOY_STATE, toy_state, 6), REG_HRO },
    { BRDATA (TOY_DATA, toy_data, 8, 8, TOY_LNT), REG_HRO },
    { NULL}
    };

MTAB sys_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "JCLK_DFLT", "JCLK_DFLT",
      &sys_set_jclk_dflt, &sys_show_jclk_dflt },
    { 0 }
    };

DEVICE sys_dev = {
    "SYSTEM", &sys_unit, sys_reg, sys_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &sys_reset,
    NULL, NULL, NULL,
    NULL, 0, 0,
    NULL, NULL, NULL
    };

/* Switch and display registers - many */

t_stat SR_rd (int32 *data, int32 pa, int32 access)
{
*data = SR;
return SCPE_OK;
}

t_stat DR_wr (int32 data, int32 pa, int32 access)
{
DR = data;
return SCPE_OK;
}

/* GPR's - 11/04, 11/05 */

t_stat REG_rd (int32 *data, int32 pa, int32 access)
{
*data = R[pa & 07];
return SCPE_OK;
}

t_stat REG_wr (int32 data, int32 pa, int32 access)
{
int32 reg = pa & 07;

if (access == WRITE)
    R[reg] = data;
else if (pa & 1)
    R[reg] = (R[reg] & 0377) | (data << 8);
else R[reg] = (R[reg] & ~0377) | data;
return SCPE_OK;
}

/* CPU control registers - 11/24 */

t_stat CPU24_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 013:                                           /* CPUERR */
        *data = 0;
        return SCPE_OK;
        }                                               /* end switch PA */

*data = 0;
return SCPE_NXM;                                        /* unimplemented */
}

t_stat CPU24_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 013:                                           /* CPUERR */
        return SCPE_OK;
        }                                               /* end switch pa */

return SCPE_NXM;                                        /* unimplemented */
}

/* CPU control registers - 11/44 */

t_stat CPU44_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 002:                                           /* MEMERR */
        *data = MEMERR;
        return SCPE_OK;

    case 003:                                           /* CCR */
        *data = CCR & CCR44_RD;
        return SCPE_OK;

    case 004:                                           /* MAINT */
        *data = MAINT & CMR44_RD;
        return SCPE_OK;

    case 005:                                           /* Hit/miss */
        *data = HITMISS;
        return SCPE_OK;

    case 006:                                           /* CDR */
        *data = 0;
        return SCPE_OK;

    case 013:                                           /* CPUERR */
        if (CPUERR & CPUE_YEL)                          /* 11/44 stack err */
            CPUERR = (CPUERR & ~CPUE_YEL) | CPUE_RED;   /* in <2> not <3> */
        if (CPUERR & (CPUE_ODD|CPUE_NXM|CPUE_TMO))      /* additional flag */
            CPUERR = CPUERR | CPUE44_BUSE;
        *data = CPUERR & CPUE_IMP;
        return SCPE_OK;

    case 015:                                           /* PIRQ */
        *data = PIRQ;
        return SCPE_OK;
        }                                               /* end switch PA */

*data = 0;
return SCPE_NXM;                                        /* unimplemented */
}

t_stat CPU44_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 002:                                           /* MEMERR */
        MEMERR = 0;
        return SCPE_OK;

    case 003:                                           /* CCR */
        ODD_MRG (CCR, data);
        CCR = data & CCR44_WR;
        return SCPE_OK;

    case 004:                                           /* MAINT */
        ODD_MRG (MAINT, data);
        MAINT = data & CMR44_WR;
        return SCPE_OK;

    case 005:                                           /* Hit/miss */
        return SCPE_OK;

    case 013:                                           /* CPUERR */
        CPUERR = 0;
        return SCPE_OK;

    case 015:                                           /* PIRQ */
        ODD_WO (data);
        put_PIRQ (data);
        return SCPE_OK;
        }

return SCPE_NXM;                                        /* unimplemented */
}

/* CPU control registers - 11/45 */

t_stat CPU45_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 014:                                           /* MBRK */
        *data = MBRK;
        return SCPE_OK;

    case 015:                                           /* PIRQ */
        *data = PIRQ;
        return SCPE_OK;

    case 016:                                           /* STKLIM */
        *data = STKLIM & STKLIM_RW;
        return SCPE_OK;
        }                                               /* end switch PA */

*data = 0;
return SCPE_NXM;                                        /* unimplemented */
}

t_stat CPU45_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 015:                                           /* PIRQ */
        ODD_WO (data);
        put_PIRQ (data);
        return SCPE_OK;

    case 016:                                           /* STKLIM */
        ODD_WO (data);
        STKLIM = data & STKLIM_RW;
        return SCPE_OK;
        }                                               /* end switch pa */

return SCPE_NXM;                                        /* unimplemented */
}

/* CPU control registers - 11/60 */

t_stat CPU60_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 000:                                           /* WCS */
        *data = WCS & WCS60_RD;
        return SCPE_OK;

    case 002:                                           /* MEMERR */
        *data = MEMERR & MEME60_RD;
        return SCPE_OK;

    case 003:                                           /* CCR */
        *data = CCR & CCR60_RD;
        return SCPE_OK;

    case 005:                                           /* Hit/miss */
        *data = HITMISS;
        return SCPE_OK;

    case 013:                                           /* CPUERR */
        if (CPUERR & CPUE_NXM)                          /* TMO only */
            CPUERR = (CPUERR & ~CPUE_NXM) | CPUE_TMO;
        *data = CPUERR & CPUE60_RD;
        return SCPE_OK;

    case 016:                                           /* STKLIM */
        *data = STKLIM & STKLIM_RW;
        return SCPE_OK;
        }                                               /* end switch PA */

*data = 0;
return SCPE_NXM;                                        /* unimplemented */
}

t_stat CPU60_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 000:                                           /* WCS */
        WCS = data & WCS60_WR;
        return SCPE_OK;

    case 002:                                           /* MEMERR */
        MEMERR = 0;
        return SCPE_OK;

    case 003:                                           /* CCR */
        ODD_IGN (data);
        CCR = data & CCR60_WR;
        return SCPE_OK;

    case 005:                                           /* Hit/miss */
        return SCPE_OK;

    case 013:                                           /* CPUERR */
        CPUERR = 0;
        return SCPE_OK;

    case 014:                                           /* MBRK */
        MBRK = data & MBRK60_WR;
        return SCPE_OK;

    case 016:                                           /* STKLIM */
        ODD_WO (data);
        STKLIM = data & STKLIM_RW;
        return SCPE_OK;
        }                                               /* end switch pa */

return SCPE_NXM;                                        /* unimplemented */
}

/* CPU control registers - 11/70 */

t_stat CPU70_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 000:                                           /* low error */
        *data = 0;
        return SCPE_OK;

    case 001:                                           /* high error */
        *data = 0;
        return SCPE_OK;

    case 002:                                           /* MEMERR */
        *data = MEMERR;
        return SCPE_OK;

    case 003:                                           /* CCR */
        *data = CCR;
        return SCPE_OK;

    case 004:                                           /* MAINT */
        *data = 0;
        return SCPE_OK;

    case 005:                                           /* Hit/miss */
        *data = HITMISS;
        return SCPE_OK;

    case 010:                                           /* low size */
        *data = (MEMSIZE >> 6) - 1;
        return SCPE_OK;

    case 011:                                           /* high size */
        *data = 0;
        return SCPE_OK;

    case 012:                                           /* system ID */
        *data = SYSID;
        return SCPE_OK;

    case 013:                                           /* CPUERR */
        *data = CPUERR & CPUE_IMP;
        return SCPE_OK;

    case 014:                                           /* MBRK */
        *data = MBRK;
        return SCPE_OK;

    case 015:                                           /* PIRQ */
        *data = PIRQ;
        return SCPE_OK;

    case 016:                                           /* STKLIM */
        *data = STKLIM & STKLIM_RW;
        return SCPE_OK;
        }                                               /* end switch PA */

*data = 0;
return SCPE_NXM;                                        /* unimplemented */
}

t_stat CPU70_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 002:                                           /* MEMERR */
        ODD_WO (data);
        MEMERR = MEMERR & ~data;
        return SCPE_OK;

    case 003:                                           /* CCR */
        ODD_MRG (CCR, data);
        CCR = data;
        return SCPE_OK;

    case 004:                                           /* MAINT */
        return SCPE_OK;

    case 005:                                           /* Hit/miss */
        return SCPE_OK;

    case 010:                                           /* low size */
        return SCPE_OK;

    case 011:                                           /* high size */
        return SCPE_OK;

    case 013:                                           /* CPUERR */
        CPUERR = 0;
        return SCPE_OK;

    case 014:                                           /* MBRK */
        ODD_IGN (data);
        MBRK = data & MBRK70_WR;
        return SCPE_OK;

    case 015:                                           /* PIRQ */
        ODD_WO (data);
        put_PIRQ (data);
        return SCPE_OK;

    case 016:                                           /* STKLIM */
        ODD_WO (data);
        STKLIM = data & STKLIM_RW;
        return SCPE_OK;
        }                                               /* end switch pa */

return SCPE_NXM;                                        /* unimplemented */
}

/* CPU control registers - J11 */

t_stat CPUJ_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 002:                                           /* MEMERR */
        *data = MEMERR;
        return SCPE_OK;

    case 003:                                           /* CCR */
        *data = CCR;
        return SCPE_OK;

    case 004:                                           /* MAINT */
        *data = MAINT | MAINT_NOFPA | MAINT_BPOK | (UNIBUS? MAINT_U: MAINT_Q);
        if (CPUT (CPUT_53))
            *data |= MAINT_KDJD | MAINT_POROM;
        if (CPUT (CPUT_73))
            *data |= MAINT_KDJA | MAINT_POODT;
        if (CPUT (CPUT_73B|CPUT_83|CPUT_84))
            *data |= MAINT_KDJB | MAINT_POROM;
        if (CPUT (CPUT_93|CPUT_94))
            *data |= MAINT_KDJE | MAINT_POROM;
        return SCPE_OK;

    case 005:                                           /* Hit/miss */
        if (CPUT (CPUT_73B))                            /* must be 0 for 73B */
            *data = 0;
        else *data = HITMISS | 010;                     /* must be nz for 11/8X */
        return SCPE_OK;

    case 013:                                           /* CPUERR */
        *data = CPUERR & CPUE_IMP;
        return SCPE_OK;

    case 015:                                           /* PIRQ */
        *data = PIRQ;
        return SCPE_OK;
        }                                               /* end switch PA */

*data = 0;
return SCPE_NXM;                                        /* unimplemented */
}

t_stat CPUJ_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 017) {                              /* decode pa<4:1> */

    case 002:                                           /* MEMERR */
        MEMERR = 0;
        return SCPE_OK;

    case 003:                                           /* CCR */
        ODD_MRG (CCR, data);
        CCR = data;
        return SCPE_OK;

    case 004:                                           /* MAINT */
        return SCPE_OK;

    case 005:                                           /* Hit/miss */
        return SCPE_OK;

    case 013:                                           /* CPUERR */
        CPUERR = 0;
        return SCPE_OK;

    case 015:                                           /* PIRQ */
        ODD_WO (data);
        put_PIRQ (data);
        return SCPE_OK;
        }                                               /* end switch pa */

return SCPE_NXM;                                        /* unimplemented */
}

/* Board control registers - KDF11B */

t_stat CTLFB_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 0:                                             /* PCR */
        *data = JPCR & PCRFB_RW;
        return SCPE_OK;

    case 1:                                             /* MAINT */
        *data = MAINT;
        return SCPE_OK;

    case 2:                                             /* CDR */
        *data = SR & CDRFB_RD;
        return SCPE_OK;
        }

*data = 0;
return SCPE_NXM;
}

t_stat CTLFB_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 0:                                             /* PCR */
        ODD_MRG (JPCR, data);
        JPCR = data & PCRFB_RW;
        return SCPE_OK;
    case 1:                                             /* MAINT */
        ODD_MRG (MAINT, data);
        MAINT = data;
        return SCPE_OK;
    case 2:                                             /* CDR */
        ODD_WO (data);
        DR = data & CDRFB_WR;
        return SCPE_OK;
        }

return SCPE_NXM;
}

/* Board control registers - KDJ11B */

t_stat CTLJB_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */
 
   case 0:                                              /* CSR */
    *data = JCSR & CSRJB_RD;
    return SCPE_OK;

    case 1:                                             /* PCR */
        *data = JPCR & PCRJB_RW;
        return SCPE_OK;

    case 2:                                             /* CDR */
        *data = SR & CDRJB_RD;
        return SCPE_OK;
        }

*data = 0;
return SCPE_NXM;
}

t_stat CTLJB_wr (int32 data, int32 pa, int32 access)
{
int32 t;

switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 0:                                             /* CSR */
        ODD_MRG (JCSR, data);
        JCSR = (JCSR & ~CSRJB_WR) | (data & CSRJB_WR);
        if (JCSR & CSRJ_LTCI)                           /* force LTC int enb? */
            clk_fie = 1;
        else clk_fie = 0;
        if (JCSR & CSRJ_LTCD)                           /* force LTC reg nxm? */
            clk_fnxm = 1;
        else clk_fnxm = 0;
        t = CSRJ_LTCSEL (JCSR);                         /* get freq sel */
        if (t != 0)
            clk_tps = clk_tps_map[t];
        else clk_tps = clk_default;
        return SCPE_OK;

    case 1:                                             /* PCR */
        ODD_MRG (JPCR, data);
        JPCR = data & PCRJB_RW;
        return SCPE_OK;

    case 2:                                             /* CDR */
        ODD_WO (data);
        DR = data & CDRJB_WR;
        return SCPE_OK;
        }

return SCPE_NXM;
}

/* Board control registers - KDJ11D */

t_stat CTLJD_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 0:                                             /* CSR */
        *data = JCSR & CSRJD_RD;
        return SCPE_OK;
        }

*data = 0;
return SCPE_NXM;
}

t_stat CTLJD_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 0:                                             /* CSR */
        ODD_MRG (JCSR, data);
        JCSR = (JCSR & ~CSRJD_WR) | (data & CSRJD_WR);
        return SCPE_OK;
        }

return SCPE_NXM;
}

/* Board control registers - KDJ11E */

t_stat CTLJE_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 0:                                             /* CSR */
        *data = JCSR & CSRJE_RD;
        return SCPE_OK;

    case 1:                                             /* PCR */
        *data = JPCR & PCRJE_RW;
        return SCPE_OK;

    case 2:                                             /* CDR */
        *data = SR & CDRJE_RD;
        return SCPE_OK;

    case 3:                                             /* ASR */
        JASR = (JASR & ~ASRJE_TOY) | (toy_read () << ASRJE_V_TOY);
        *data = JASR & ASRJE_RW;            
        return SCPE_OK;
        }

*data = 0;
return SCPE_NXM;
}

t_stat CTLJE_wr (int32 data, int32 pa, int32 access)
{
int32 t;

switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 0:                                             /* CSR */
        ODD_MRG (JCSR, data);
        JCSR = (JCSR & ~CSRJE_WR) | (data & CSRJE_WR);
        if (JCSR & CSRJ_LTCI)                           /* force LTC int enb? */
            clk_fie = 1;
        else clk_fie = 0;
        if (JCSR & CSRJ_LTCD)                           /* force LTC reg nxm? */
            clk_fnxm = 1;
        else clk_fnxm = 0;
        t = CSRJ_LTCSEL (JCSR);                         /* get freq sel */
        if (t != 0)
            clk_tps = clk_tps_map[t];
        else clk_tps = clk_default;
        return SCPE_OK;

    case 1:                                             /* PCR */
        ODD_MRG (JPCR, data);
        JPCR = data & PCRJE_RW;
        return SCPE_OK;

    case 2:                                             /* CDR */
        ODD_WO (data);
        DR = data & CDRJE_WR;
        return SCPE_OK;

    case 3:                                             /* ASR */
        ODD_MRG (JASR, data);
        JASR = data & ASRJE_RW;
        toy_write (ASRJE_TOYBIT (JASR));
        return SCPE_OK;
        }

return SCPE_NXM;
}

/* Unibus adapter registers - KT24 */

t_stat UBA24_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 2:                                             /* LMAL */
        *data = uba_last & LMAL_RD;
        return SCPE_OK;
    case 3:                                             /* LMAH */
        *data = uba_last & LMAH_RD;
        return SCPE_OK;
        }

*data = 0;
return SCPE_NXM;
}

t_stat UBA24_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 3:                                             /* ASR */
        ODD_IGN (data);
        uba_last = (uba_last & ~LMAH_WR) | ((data & LMAH_WR) << 16);
        return SCPE_OK;
        }

return SCPE_NXM;
}

/* Unibus registers - KTJ11B */

t_stat UBAJ_rd (int32 *data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 0:                                             /* DCR */
        *data = UDCR & DCRKTJ_RD;
        return SCPE_OK;

    case 1:                                             /* DDR */
        *data = UDDR & DDRKTJ_RW;
        return SCPE_OK;

    case 2:                                             /* CSR */
        *data = UCSR & MCRKTJ_RD;
        return SCPE_OK;
        }

*data = 0;
return SCPE_NXM;
}

t_stat UBAJ_wr (int32 data, int32 pa, int32 access)
{
switch ((pa >> 1) & 03) {                               /* decode pa<2:1> */

    case 0:                                             /* DCR */
        ODD_MRG (UDCR, data);
        UDCR = (UDCR & ~DCRKTJ_WR) | (data & DCRKTJ_WR);
        return SCPE_OK;

    case 1:                                             /* DDR */
        ODD_MRG (UDDR, data);
        UDDR = data & DDRKTJ_RW;;
        return SCPE_OK;

    case 2:                                             /* CSR */
        ODD_MRG (UCSR, data);
        UCSR = (UCSR & ~MCRKTJ_WR) | (data & MCRKTJ_WR);
        return SCPE_OK;
        }

return SCPE_NXM;
}

/* KDJ11E TOY routines */

int32 toy_read (void)
{
int32 bit;

if (toy_state == 0) {
    struct timespec now;
    time_t curr;
    struct tm *ctm;

    sim_rtcn_get_time (&now, 0);
    curr = (time_t)now.tv_sec;

    if (curr == (time_t) -1)                            /* error? */
        return 0;
    ctm = localtime (&curr);                            /* decompose */
    if (ctm == NULL)                                    /* error? */
        return 0;
    toy_data[TOY_HSEC] = toy_set ((now.tv_nsec + 5000000) / 10000000);
    toy_data[TOY_SEC] = toy_set (ctm->tm_sec);
    toy_data[TOY_MIN] = toy_set (ctm->tm_min);
    toy_data[TOY_HR] = toy_set (ctm->tm_hour);
    toy_data[TOY_DOW] = toy_set (ctm->tm_wday);
    toy_data[TOY_DOM] = toy_set (ctm->tm_mday);
    toy_data[TOY_MON] = toy_set (ctm->tm_mon + 1);
    toy_data[TOY_YR] = toy_set (ctm->tm_year % 100);
    }
bit = toy_data[toy_state >> 3] >> (toy_state & 07);
toy_state = (toy_state + 1) % (TOY_LNT * 8);
return (bit & 1);
}

void toy_write (int32 bit)
{
toy_state = 0;
return;
}

uint8 toy_set (int32 val)
{
uint32 d1, d2;

d1 = val / 10;
d2 = val % 10;
return (uint8) ((d1 << 4) | d2);
}

/* Build I/O space entries for CPU */

t_stat cpu_build_dib (void)
{
int32 i;
t_stat r;

for (i = 0; cnf_tab[i].dib != NULL; i++) {              /* loop thru config tab */
    if (((cnf_tab[i].cpum == 0) || (cpu_type & cnf_tab[i].cpum)) &&
        ((cnf_tab[i].optm == 0) || (cpu_opt & cnf_tab[i].optm))) {
        if ((r = build_ubus_tab (&cpu_dev, cnf_tab[i].dib)))/* add to dispatch tab */
             return r;
        }
    }
return SCPE_OK;
}

/* Set/show CPU model */

t_stat cpu_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr != NULL)
    return SCPE_ARG;
if (val >= MOD_MAX)
    return SCPE_IERR;
if (val == (int32) cpu_model)
    return SCPE_OK;
cpu_model = val;
cpu_type = 1u << cpu_model;
cpu_opt = cpu_tab[cpu_model].std;
cpu_set_bus (cpu_opt);
if (MEMSIZE > cpu_tab[val].maxm)
    cpu_set_size (uptr, cpu_tab[val].maxm, NULL, NULL);
if (MEMSIZE > cpu_tab[val].maxm)
    return SCPE_INCOMP;
reset_all (0);                                          /* reset world */
return SCPE_OK;
}

t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 i, all_opt;

fprintf (st, "%s", cpu_tab[cpu_model].name);
all_opt = cpu_tab[cpu_model].opt;
for (i = 0; opt_name[2 * i] != NULL; i++) {
    if ((all_opt >> i) & 1)
        fprintf (st, ", %s",
                ((cpu_opt >> i) & 1)? opt_name[2 * i]: opt_name[(2 * i) + 1]);
    }   
return SCPE_OK;
}

/* Set/clear CPU option */

t_stat cpu_set_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if ((val & cpu_tab[cpu_model].opt) == 0)
    return SCPE_ARG;
cpu_opt = cpu_opt | val;
return SCPE_OK;
}

t_stat cpu_clr_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if ((val & cpu_tab[cpu_model].opt) == 0)
    return SCPE_ARG;
cpu_opt = cpu_opt & ~val;
return SCPE_OK;
}

/* Memory allocation */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
uint32 i, clim;
uint16 *nM;

if ((val <= 0) ||
    (val > ((int32) cpu_tab[cpu_model].maxm)) ||
    ((val & 07777) != 0))
    return SCPE_ARG;
if (val > ((int32) (cpu_tab[cpu_model].maxm - IOPAGESIZE)))
    val = (int32) (cpu_tab[cpu_model].maxm - IOPAGESIZE);
for (i = val; i < MEMSIZE; i = i + 2)
    mc = mc | M[i >> 1];
if ((mc != 0) && !get_yn ("Really truncate memory [N]?", FALSE))
    return SCPE_OK;
nM = (uint16 *) calloc (val >> 1, sizeof (uint16));
if (nM == NULL)
    return SCPE_MEM;
clim = (((t_addr) val) < MEMSIZE)? (uint32)val: MEMSIZE;
for (i = 0; i < clim; i = i + 2)
    nM[i >> 1] = M[i >> 1];
free (M);
M = nM;
MEMSIZE = val;
if (!(sim_switches & SIM_SW_REST))                      /* unless restore, */
    cpu_set_bus (cpu_opt);                              /* alter periph config */
return SCPE_OK;
}

/* Bus configuration, disable Unibus or Qbus devices */

t_stat cpu_set_bus (int32 opt)
{
DEVICE *dptr;
uint32 i, mask;

if (opt & BUS_U)                                        /* Unibus variant? */
    mask = DEV_UBUS;
else if (MEMSIZE <= UNIMEMSIZE)                         /* 18b Qbus devices? */
    mask = DEV_QBUS | DEV_Q18;
else mask = DEV_QBUS;                                   /* must be 22b */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    if ((dptr->flags & DEV_DISABLE) &&                  /* disable-able? */
        !(dptr->flags & DEV_DIS) &&                     /* enabled? */
        ((dptr->flags & mask) == 0)) {                  /* not allowed? */
        sim_printf ("Disabling %s\n", sim_dname (dptr));
        dptr->flags = dptr->flags | DEV_DIS;
        }
    }
return SCPE_OK;
}

/* System reset */

t_stat sys_reset (DEVICE *dptr)
{
int32 i;

CCR = 0;
HITMISS = 0;
CPUERR = 0;
MEMERR = 0;
if (!CPUT (CPUT_J))
    MAINT = 0;
MBRK = 0;
WCS = 0;
if (CPUT (CPUT_JB|CPUT_JE))
    JCSR = JCSR_dflt;
else JCSR = 0;
JPCR = 0;
JASR = 0;
UDCR = 0;
UDDR = 0;
UCSR = 0;
uba_last = 0;
DR = 0;
toy_state = 0;
for (i = 0; i < UBM_LNT_LW; i++)
    ub_map[i] = 0;
for (i = 0; i < TOY_LNT; i++)
    toy_data[i] = 0;
return SCPE_OK;
}

/* Set/show JCLK default values */

t_stat sys_set_jclk_dflt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 i;

if ((CPUT (CPUT_JB|CPUT_JE)) && cptr) {
    for (i = 0; i < 4; i++) {
        if (strncmp (cptr, jcsr_val[i], strlen (cptr)) == 0) {
            JCSR_dflt = i << CSRJ_V_LTCSEL;
            return SCPE_OK;
            }
        }
    }
return SCPE_ARG;
}

t_stat sys_show_jclk_dflt (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (CPUT (CPUT_JB|CPUT_JE))
    fprintf (st, "JCLK default=%s\n", jcsr_val[CSRJ_LTCSEL (JCSR_dflt)]);
else fprintf (st, "Not implemented\n");
return SCPE_OK;
}
