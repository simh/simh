/* vax780_sbi.c: VAX 11/780 SBI

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

   This module contains the VAX 11/780 system-specific registers and devices.

   sbi                  bus controller

   29-Mar-2015  RMS     Added in-exception test to machine check
   16-Dec-2014  RMS     Removed TQ boot entry (VMB doesn't support tape boot)
   21-Mar-2011  RMS     Added autoreboot capability (Mark Pizzolato)
   04-Feb-2011  MP      Added RQB, RQC, and RQD as bootable controllers
   31-May-2008  RMS     Fixed machine_check calling sequence (Peter Schorn)
   03-May-2006  RMS     Fixed writes to ACCS
   28-May-2008  RMS     Inlined physical memory routines
*/

#include "vax_defs.h"

#ifdef DONT_USE_INTERNAL_ROM
#define BOOT_CODE_FILENAME "vmb.exe"
#else /* !DONT_USE_INTERNAL_ROM */
#include "vax_vmb_exe.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#endif /* DONT_USE_INTERNAL_ROM */

/* 11/780 specific IPRs */

/* Writeable control store */

#define WCSA_RW         0xFFFF                          /* writeable */
#define WCSA_ADDR       0x1FFF                          /* addr */
#define WCSA_CTR        0x6000                          /* counter */
#define WCSA_CTR_INC    0x2000                          /* increment */
#define WCSA_CTR_MAX    0x6000                          /* max value */
#define WCSD_RD_VAL     0xFF                            /* fixed read val */
#define WCSD_WR         0xFFFFFFFF                      /* write */
#define MBRK_RW         0x1FFF                          /* microbreak */

/* System registers */

#define SBIFS_RD        (0x031F0000|SBI_FAULTS)         /* SBI faults */
#define SBIFS_WR        0x03140000
#define SBIFS_W1C       0x00080000

#define SBISC_RD        0xFFFF0000                      /* SBI silo comp */
#define SBISC_WR        0x7FFF0000
#define SBISC_LOCK      0x80000000                      /* lock */

#define SBIMT_RD        0xFFFFFF00                      /* SBI maint */
#define SBIMT_WR        0xFFFFF900

#define SBIER_CRDIE     0x00008000                      /* SBI error, CRD IE */
#define SBIER_CRD       0x00004000                      /* CRD */
#define SBIER_RDS       0x00002000                      /* RDS */
#define SBIER_TMO       0x00001000                      /* timeout */
#define SBIER_STA       0x00000C00                      /* timeout status (0) */
#define SBIER_CNF       0x00000100                      /* error confirm */
#define SBIER_IBRDS     0x00000080
#define SBIER_IBTMO     0x00000040
#define SBIER_IBSTA     0x00000030
#define SBIER_IBCNF     0x00000008
#define SBIER_MULT      0x00000004                      /* multiple errors */
#define SBIER_FREE      0x00000002                      /* SBI free */
#define SBIER_RD        0x0000FDFE
#define SBIER_WR        0x00008000
#define SBIER_W1C       0x000070C0
#define SBIER_TMOW1C    (SBIER_TMO|SBIER_STA|SBIER_CNF|SBIER_MULT)
#define SBIER_IBTW1C    (SBIER_IBTMO|SBIER_STA|SBIER_IBCNF)

#define SBITMO_V_MODE   30                              /* mode */
#define SBITMO_VIRT     0x20000000                      /* physical */

#define SBIQC_MBZ       0xC0000007                      /* MBZ */

/* VAX-11/780 boot device definitions */

struct boot_dev {
    const char          *name;
    int32               code;
    int32               let;
    };

uint32 wcs_addr = 0;
uint32 wcs_data = 0;
uint32 wcs_mbrk = 0;
uint32 nexus_req[NEXUS_HLVL];                           /* nexus int req */
uint32 sbi_fs = 0;                                      /* SBI fault status */
uint32 sbi_sc = 0;                                      /* SBI silo comparator */
uint32 sbi_mt = 0;                                      /* SBI maintenance */
uint32 sbi_er = 0;                                      /* SBI error status */
uint32 sbi_tmo = 0;                                     /* SBI timeout addr */
int32 sys_model = 0;                                    /* 780 or 785 */
static char cpu_boot_cmd[CBUFSIZE]  = { 0 };            /* boot command */

static t_stat (*nexusR[NEXUS_NUM])(int32 *dat, int32 ad, int32 md);
static t_stat (*nexusW[NEXUS_NUM])(int32 dat, int32 ad, int32 md);

static struct boot_dev boot_tab[] = {
    { "RP", BOOT_MB, 0 },
    { "HK", BOOT_HK, 0 },
    { "RL", BOOT_RL, 0 },
    { "RQ", BOOT_UDA, 1 << 24 },
    { "RQB", BOOT_UDA, 1 << 24 },
    { "RQC", BOOT_UDA, 1 << 24 },
    { "RQD", BOOT_UDA, 1 << 24 },
    { "CS", BOOT_CS, 0 },
    { NULL }
    };

extern int32 tmr_int, tti_int, tto_int;

t_stat sbi_reset (DEVICE *dptr);
const char *sbi_description (DEVICE *dptr);
void sbi_set_tmo (int32 pa);
void uba_eval_int (void);
t_stat vax780_boot (int32 flag, CONST char *ptr);
t_stat vax780_boot_parse (int32 flag, const char *ptr);

extern t_stat vax780_fload (int32 flag, CONST char *cptr);
extern int32 iccs_rd (void);
extern int32 nicr_rd (void);
extern int32 icr_rd (void);
extern int32 todr_rd (void);
extern int32 rxcs_rd (void);
extern int32 rxdb_rd (void);
extern int32 txcs_rd (void);
extern void iccs_wr (int32 dat);
extern void nicr_wr (int32 dat);
extern void todr_wr (int32 dat);
extern void rxcs_wr (int32 dat);
extern void txcs_wr (int32 dat);
extern void txdb_wr (int32 dat);
extern void init_mbus_tab (void);
extern void init_ubus_tab (void);
extern t_stat build_mbus_tab (DEVICE *dptr, DIB *dibp);
extern t_stat build_ubus_tab (DEVICE *dptr, DIB *dibp);

/* SBI data structures

   sbi_dev      SBI device descriptor
   sbi_unit     SBI unit
   sbi_reg      SBI register list
*/

UNIT sbi_unit = { UDATA (NULL, 0, 0) };

REG sbi_reg[] = {
    { HRDATA (NREQ14, nexus_req[0], 16) },
    { HRDATA (NREQ15, nexus_req[1], 16) },
    { HRDATA (NREQ16, nexus_req[2], 16) },
    { HRDATA (NREQ17, nexus_req[3], 16) },
    { HRDATA (WCSA, wcs_addr, 16) },
    { HRDATA (WCSD, wcs_data, 32) },
    { HRDATA (MBRK, wcs_mbrk, 13) },
    { HRDATA (SBIFS, sbi_fs, 32) },
    { HRDATA (SBISC, sbi_sc, 32) },
    { HRDATA (SBIMT, sbi_mt, 32) },
    { HRDATA (SBIER, sbi_er, 32) },
    { HRDATA (SBITMO, sbi_tmo, 32) },
    { BRDATA (BOOTCMD, cpu_boot_cmd, 16, 8, CBUFSIZE), REG_HRO },
    { NULL }
    };

DEVICE sbi_dev = {
    "SBI", &sbi_unit, sbi_reg, NULL,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &sbi_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &sbi_description
    };

/* Special boot command, overrides regular boot */

CTAB vax780_cmd[] = {
    { "BOOT", &vax780_boot, RU_BOOT,
      "bo{ot} <device>{/R5:flg} boot device\n"
      "                         type HELP CPU to see bootable devices\n", NULL, &run_cmd_message },
    { "FLOAD", &vax780_fload, 0,
      "fl{oad} <file> {<start>} load file from console floppy\n" },
    { NULL }
    };

/* The VAX 11/780 has three sources of interrupts

   - internal device interrupts (CPU, console, clock)
   - nexus interupts (e.g., memory controller, MBA, UBA)
   - external device interrupts (Unibus)

   Internal devices vector to fixed SCB locations.

   Nexus interrupts vector to an SCB location based on this
   formula: SCB_NEXUS + ((IPL - 0x14) * 0x40) + (TR# * 0x4)

   External device interrupts do not vector directly.
   Instead, the interrupt handler for a given UBA IPL
   reads a vector register that contains the Unibus vector
   for that IPL.
*/
/* Find highest priority vectorable interrupt */

int32 eval_int (void)
{
int32 ipl = PSL_GETIPL (PSL);
int32 i, t;

static const int32 sw_int_mask[IPL_SMAX] = {
    0xFFFE, 0xFFFC, 0xFFF8, 0xFFF0,                     /* 0 - 3 */
    0xFFE0, 0xFFC0, 0xFF80, 0xFF00,                     /* 4 - 7 */
    0xFE00, 0xFC00, 0xF800, 0xF000,                     /* 8 - B */
    0xE000, 0xC000, 0x8000                              /* C - E */
    };

if (hlt_pin)                                            /* hlt pin int */
    return IPL_HLTPIN;
if ((ipl < IPL_MEMERR) && mem_err)                      /* mem err int */
    return IPL_MEMERR;
if ((ipl < IPL_CRDERR) && crd_err)                      /* crd err int */
    return IPL_CRDERR;
if ((ipl < IPL_CLKINT) && tmr_int)                      /* clock int */
    return IPL_CLKINT;
uba_eval_int ();                                        /* update UBA */
for (i = IPL_HMAX; i >= IPL_HMIN; i--) {                /* chk hwre int */
    if (i <= ipl)                                       /* at ipl? no int */
        return 0;
    if (nexus_req[i - IPL_HMIN])                        /* req != 0? int */
        return i;
    }
if ((ipl < IPL_TTINT) && (tti_int || tto_int))          /* console int */
    return IPL_TTINT;
if (ipl >= IPL_SMAX)                                    /* ipl >= sw max? */
    return 0;
if ((t = SISR & sw_int_mask[ipl]) == 0)
    return 0;                                           /* eligible req */
for (i = IPL_SMAX; i > ipl; i--) {                      /* check swre int */
    if ((t >> i) & 1)                                    /* req != 0? int */
        return i;
    }
return 0;
}

/* Return vector for highest priority hardware interrupt at IPL lvl */

int32 get_vector (int32 lvl)
{
int32 i, l;

if (lvl == IPL_MEMERR) {                                /* mem error? */
    mem_err = 0;
    return SCB_MEMERR;
    }
if (lvl == IPL_CRDERR) {                                /* CRD error? */
    crd_err = 0;
    return SCB_CRDERR;
    }
if (lvl == IPL_CLKINT) {                                /* clock? */
    tmr_int = 0;                                        /* clear req */
    return SCB_INTTIM;                                  /* return vector */
    }
if (lvl > IPL_HMAX) {                                   /* error req lvl? */
    ABORT (STOP_UIPL);                                  /* unknown intr */
    }
if ((lvl <= IPL_HMAX) && (lvl >= IPL_HMIN)) {           /* nexus? */
    l = lvl - IPL_HMIN;
    for (i = 0; nexus_req[l] && (i < NEXUS_NUM); i++) {
        if ((nexus_req[l] >> i) & 1) {
            nexus_req[l] = nexus_req[l] & ~(1u << i);
            return SCB_NEXUS + (l << 6) + (i << 2);     /* return vector */
            }
        }
    }
if (lvl == IPL_TTINT) {                                 /* console? */
    if (tti_int) {                                      /* input? */
        tti_int = 0;                                    /* clear req */
        return SCB_TTI;                                 /* return vector */
        }
    if (tto_int) {                                      /* output? */
        tto_int = 0;                                    /* clear req */
        return SCB_TTO;                                 /* return vector */
        }
    }
return 0;
}

/* Read 780-specific IPR's */

int32 ReadIPR (int32 rg)
{
int32 val;

switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        val = iccs_rd ();
        break;

    case MT_NICR:                                       /* NICR */
        val = nicr_rd ();
        break;

    case MT_ICR:                                        /* ICR */
        val = icr_rd ();
        break;

    case MT_TODR:                                       /* TODR */
        val = todr_rd ();
        break;

    case MT_ACCS:                                       /* ACCS (not impl) */
        val = 0;
        break;

    case MT_WCSA:                                       /* WCSA */
        val = wcs_addr & WCSA_RW;
        break;

    case MT_WCSD:                                       /* WCSD */
        val = WCSD_RD_VAL;
        break;

    case MT_RXCS:                                       /* RXCS */
        val = rxcs_rd ();
        break;

    case MT_RXDB:                                       /* RXDB */
        val = rxdb_rd ();
        break;

    case MT_TXCS:                                       /* TXCS */
        val = txcs_rd ();
        break;

    case MT_SBIFS:                                      /* SBIFS */
        val = sbi_fs & SBIFS_RD;
        break;

    case MT_SBIS:                                       /* SBIS */
        val = 0;
        break;

    case MT_SBISC:                                      /* SBISC */
        val = sbi_sc & SBISC_RD;
        break;

    case MT_SBIMT:                                      /* SBIMT */
        val = sbi_mt & SBIMT_RD;
        break;

    case MT_SBIER:                                      /* SBIER */
        val = sbi_er & SBIER_RD;
        break;

    case MT_SBITA:                                      /* SBITA */
        val = sbi_tmo;
        break;

    case MT_MBRK:                                       /* MBRK */
        val = wcs_mbrk & MBRK_RW;
        break;

    case MT_SID:                                        /* SID */
        if (sys_model)
            val = VAX780_SID | VAX785_TYP | VAX780_ECO | VAX780_PLANT | VAX780_SN;
        else
            val = VAX780_SID | VAX780_TYP | VAX780_ECO | VAX780_PLANT | VAX780_SN;
        break;

    default:
        RSVD_OPND_FAULT(ReadIPR);
        }

return val;
}

/* Write 780-specific IPR's */

void WriteIPR (int32 rg, int32 val)
{
switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        iccs_wr (val);
        break;

    case MT_NICR:                                       /* NICR */
        nicr_wr (val);
        break;

    case MT_TODR:                                       /* TODR */
        todr_wr (val);
        break;

    case MT_ACCS:                                       /* ACCS (not impl) */
        break;

    case MT_WCSA:                                       /* WCSA */
        wcs_addr = val & WCSA_RW;
        break;

    case MT_WCSD:                                       /* WCSD */
        wcs_data = val & WCSD_WR;
        wcs_addr = (wcs_addr & ~WCSA_CTR) |
            ((wcs_addr + WCSA_CTR_INC) & WCSA_CTR);
        if ((wcs_addr & WCSA_CTR) == WCSA_CTR_MAX)
            wcs_addr = (wcs_addr & ~WCSA_ADDR) |
                       ((wcs_addr + 1) & WCSA_ADDR);
        break;

    case MT_RXCS:                                       /* RXCS */
        rxcs_wr (val);
        break;

    case MT_TXCS:                                       /* TXCS */
        txcs_wr (val);
        break;

    case MT_TXDB:                                       /* TXDB */
        txdb_wr (val);
        break;

    case MT_SBIFS:                                      /* SBIFS */
        sbi_fs = (sbi_fs & ~SBIFS_WR) | (val & SBIFS_WR);
        sbi_fs = sbi_fs & ~(val & SBIFS_W1C);
        break;

    case MT_SBISC:                                      /* SBISC */
        sbi_sc = (sbi_sc & ~(SBISC_LOCK|SBISC_WR)) | (val & SBISC_WR);
        break;

    case MT_SBIMT:                                      /* SBIMT */
        sbi_mt = (sbi_mt & ~SBIMT_WR) | (val & SBIMT_WR);
        break;

    case MT_SBIER:                                      /* SBIER */
        sbi_er = (sbi_er & ~SBIER_WR) | (val & SBIER_WR);
        sbi_er = sbi_er & ~(val & SBIER_W1C);
        if (val & SBIER_TMO)
            sbi_er = sbi_er & ~SBIER_TMOW1C;
        if (val & SBIER_IBTMO)
            sbi_er = sbi_er & ~SBIER_IBTW1C;
        if ((sbi_er & SBIER_CRDIE) && (sbi_er & SBIER_CRD))
            crd_err = 1;
        else crd_err = 0;
        break;

    case MT_SBIQC:                                      /* SBIQC */
        if (val & SBIQC_MBZ) {
            RSVD_OPND_FAULT(WriteIPR);
            }
        WriteLP (val, 0);
        WriteLP (val + 4, 0);
        break;

    case MT_MBRK:                                       /* MBRK */
        wcs_mbrk = val & MBRK_RW;
        break;

    default:
        RSVD_OPND_FAULT(WriteIPR);
        }

return;
}

/* ReadReg - read register space

   Inputs:
        pa      =       physical address
        lnt     =       length (BWLQ)
   Output:
        longword of data
*/

int32 ReadReg (uint32 pa, int32 lnt)
{
int32 nexus, val;

if (ADDR_IS_REG (pa)) {                                 /* reg space? */
    nexus = NEXUS_GETNEX (pa);                          /* get nexus */
    if (nexusR[nexus] &&                                /* valid? */
        (nexusR[nexus] (&val, pa, lnt) == SCPE_OK)) {
        SET_IRQL;
        return val;
        }
    }
sbi_set_tmo (pa);                                       /* timeout */
MACH_CHECK (MCHK_RD_F);                                 /* machine check */
return 0;
} 

/* WriteReg - write register space

   Inputs:
        pa      =       physical address
        val     =       data to write, right justified in 32b longword
        lnt     =       length (BWLQ)
   Outputs:
        none
*/

void WriteReg (uint32 pa, int32 val, int32 lnt)
{
int32 nexus;

if (ADDR_IS_REG (pa)) {                                 /* reg space? */
    nexus = NEXUS_GETNEX (pa);                          /* get nexus */
    if (nexusW[nexus] &&                                /* valid? */
        (nexusW[nexus] (val, pa, lnt) == SCPE_OK)) {
        SET_IRQL;
        return;
        }
    }
sbi_set_tmo (pa);                                       /* timeout */
mem_err = 1;                                            /* interrupt */
eval_int ();
return;
}

/* Set SBI timeout - machine checks only on reads */

void sbi_set_tmo (int32 pa)
{
if ((sbi_er & SBIER_TMO) == 0) {                        /* not yet set? */
    sbi_tmo = pa >> 2;                                  /* save addr */
    if (mchk_ref == REF_V)                              /* virt? add mode */
        sbi_tmo |= SBITMO_VIRT | (PSL_GETCUR (PSL) << SBITMO_V_MODE);
    sbi_er |= SBIER_TMO;                                /* set tmo flag */
    }
else sbi_er |= SBIER_MULT;                              /* yes, multiple */
return;
}

/* Set SBI error confirmation - always machine checks */

void sbi_set_errcnf (void)
{
if (sbi_er & SBIER_CNF)
    sbi_er |= SBIER_MULT;
else sbi_er |= SBIER_CNF;
MACH_CHECK (MCHK_RD_F);
return;
}

/* Machine check

   Error status word format
   <2:0> =      ASTLVL
   <3> =        PME
   <6:4> =      arith trap code
   Rest will be zero
*/

int32 machine_check (int32 p1, int32 opc, int32 cc, int32 delta)
{
int32 acc, err;

if (in_ie)                                              /* in exc? panic */
    ABORT (STOP_INIE);
err = (GET_TRAP (trpirq) << 4) | (pme << 3) | ASTLVL;   /* error word */
cc = intexc (SCB_MCHK, cc, 0, IE_SVE);                  /* take exception */
acc = ACC_MASK (KERN);                                  /* in kernel mode */
in_ie = 1;
SP = SP - 44;                                           /* push 11 words */
Write (SP, 40, L_LONG, WA);                             /* # bytes */
Write (SP + 4, p1, L_LONG, WA);                         /* mcheck type */
Write (SP + 8, err, L_LONG, WA);                        /* CPU error status */
Write (SP + 12, 0, L_LONG, WA);                         /* uPC */
Write (SP + 16, mchk_va, L_LONG, WA);                   /* VA */
Write (SP + 20, 0, L_LONG, WA);                         /* D register */
Write (SP + 24, mapen, L_LONG, WA);                     /* TB status 1 */
Write (SP + 28, 0, L_LONG, WA);                         /* TB status 2 */
Write (SP + 32, sbi_tmo, L_LONG, WA);                   /* SBI timeout addr */
Write (SP + 36, 0, L_LONG, WA);                         /* cache status */
Write (SP + 40, sbi_er, L_LONG, WA);                    /* SBI error */
in_ie = 0;
sbi_er = sbi_er & ~SBIER_TMOW1C;                        /* clr SBIER<tmo> etc */
return cc;
}

/* Console entry - only reached if CONHALT is set (AUTORESTART is set */

int32 con_halt (int32 code, int32 cc)
{
if ((cpu_boot_cmd[0] == 0) ||                           /* saved boot cmd? */
    (vax780_boot_parse (0, cpu_boot_cmd) != SCPE_OK) || /* reparse the boot cmd */ 
    (reset_all (0) != SCPE_OK) ||                       /* reset the world */
    (cpu_boot (0, NULL) != SCPE_OK))                    /* set up boot code */
    ABORT (STOP_BOOT);                                  /* any error? */
sim_printf ("Rebooting...\n");
return cc;
}

/* Special boot command - linked into SCP by initial reset

   Syntax: BOOT <device>{/R5:val}

   Sets up R0-R5, calls SCP boot processor with effective BOOT CPU
*/

t_stat vax780_boot (int32 flag, CONST char *ptr)
{
t_stat r;

r = vax780_boot_parse (flag, ptr);                      /* parse the boot cmd */
if (r != SCPE_OK) {                                     /* error? */
    if (r >= SCPE_BASE) {                               /* message available? */
        sim_printf ("%s\n", sim_error_text (r));
        r |= SCPE_NOMESSAGE;
        }
    return r;
    }
strncpy (cpu_boot_cmd, ptr, CBUFSIZE-1);                /* save for reboot */
return run_cmd (flag, "CPU");
}

/* Parse boot command, set up registers - also used on reset */

t_stat vax780_boot_parse (int32 flag, const char *ptr)
{
char gbuf[CBUFSIZE];
char *slptr;
const char *regptr;
int32 i, r5v, unitno;
uint32 ba;
DEVICE *dptr;
UNIT *uptr;
DIB *dibp;
t_stat r;

if (!ptr || !*ptr)
    return SCPE_2FARG;
if ((ptr = get_sim_sw (ptr)) == NULL)                   /* get switches */
    return SCPE_INVSW;
regptr = get_glyph (ptr, gbuf, 0);                      /* get glyph */
if ((slptr = strchr (gbuf, '/'))) {                     /* found slash? */
    regptr = strchr (ptr, '/');                         /* locate orig */
    *slptr = 0;                                         /* zero in string */
    }
dptr = find_unit (gbuf, &uptr);                         /* find device */
if ((dptr == NULL) || (uptr == NULL))
    return SCPE_ARG;
dibp = (DIB *) dptr->ctxt;                              /* get DIB */
if (dibp == NULL)
    ba = 0;
else
    ba = dibp->ba;
unitno = (int32) (uptr - dptr->units);
r5v = 0;
/* coverity[NULL_RETURNS] */ 
if ((strncmp (regptr, "/R5:", 4) == 0) ||
    (strncmp (regptr, "/R5=", 4) == 0) ||
    (strncmp (regptr, "/r5:", 4) == 0) ||
    (strncmp (regptr, "/r5=", 4) == 0)) {
    r5v = (int32) get_uint (regptr + 4, 16, LMASK, &r);
    if (r != SCPE_OK)
        return r;
    }
else 
    if (*regptr == '/') {
        r5v = (int32) get_uint (regptr + 1, 16, LMASK, &r);
        if (r != SCPE_OK)
            return r;
        }
    else {
        if (*regptr != 0)
            return SCPE_ARG;
        }
for (i = 0; boot_tab[i].name != NULL; i++) {
    if (strcmp (dptr->name, boot_tab[i].name) == 0) {
        R[0] = boot_tab[i].code;
        if (dptr->flags & DEV_MBUS) {
            R[1] = ba + TR_MBA0;
            R[2] = unitno;
            }
        else {
            R[1] = TR_UBA;
            R[2] = boot_tab[i].let | (ba & UBADDRMASK);
            }
        R[3] = unitno;
        R[4] = 0;
        R[5] = r5v;
        return SCPE_OK;
        }
    }
return SCPE_NOFNC;
}

/* Bootstrap - finish up bootstrap process */

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
t_stat r;

r = cpu_load_bootcode (BOOT_CODE_FILENAME, BOOT_CODE_ARRAY, BOOT_CODE_SIZE, FALSE, 0x200);
if (r != SCPE_OK)
    return r;
SP = PC = 512;
return SCPE_OK;
}

/* SBI reset */

t_stat sbi_reset (DEVICE *dptr)
{
wcs_addr = 0;
wcs_data = 0;
wcs_mbrk = 0;
sbi_fs = 0;
sbi_sc = 0;
sbi_mt = 0;
sbi_er = 0;
sbi_tmo = 0;
sim_vm_cmd = vax780_cmd;
return SCPE_OK;
}

const char *sbi_description (DEVICE *dptr)
{
return "Synchronous Backplane Interconnect";
}

/* Show nexus */

t_stat show_nexus (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "nexus=%d, address=%X", val, NEXUSBASE + ((1 << REG_V_NEXUS) * val));
return SCPE_OK;
}

/* Init nexus tables */

void init_nexus_tab (void)
{
uint32 i;

for (i = 0; i < NEXUS_NUM; i++) {
    nexusR[i] = NULL;
    nexusW[i] = NULL;
    }
return;
}

/* Build nexus tables

   Inputs:
        dptr    =       pointer to device
        dibp    =       pointer to DIB
   Outputs:
        status
*/


t_stat build_nexus_tab (DEVICE *dptr, DIB *dibp)
{
uint32 idx;

if ((dptr == NULL) || (dibp == NULL))
    return SCPE_IERR;
idx = dibp->ba;
if (idx >= NEXUS_NUM)
    return SCPE_IERR;
if ((nexusR[idx] && dibp->rd &&                         /* conflict? */
    (nexusR[idx] != dibp->rd)) ||
    (nexusW[idx] && dibp->wr &&
    (nexusW[idx] != dibp->wr))) {
    sim_printf ("Nexus %s conflict at %d\n", sim_dname (dptr), dibp->ba);
    return SCPE_STOP;
    }
if (dibp->rd)                                           /* set rd dispatch */
    nexusR[idx] = dibp->rd;
if (dibp->wr)                                           /* set wr dispatch */
    nexusW[idx] = dibp->wr;
return SCPE_OK;
}

/* Build dib_tab from device list */

t_stat build_dib_tab (void)
{
uint32 i;
DEVICE *dptr;
DIB *dibp;
t_stat r;

init_nexus_tab ();
init_ubus_tab ();
init_mbus_tab ();
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* defined, enabled? */
        if (dptr->flags & DEV_NEXUS) {                  /* Nexus? */
            if ((r = build_nexus_tab (dptr, dibp)))     /* add to dispatch table */
                return r;
            }
        else if (dptr->flags & DEV_MBUS) {              /* Massbus? */
            if ((r = build_mbus_tab (dptr, dibp)))
                return r;
            }
        else {                                          /* no, Unibus device */
            if ((r = build_ubus_tab (dptr, dibp)))      /* add to dispatch tab */
                return r;
            }                                           /* end else */
        }                                               /* end if enabled */
    }                                                   /* end for */
return SCPE_OK;
}

t_stat cpu_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr == NULL) return SCPE_ARG;
if (strcmp(cptr, "780") == 0) {
   sys_model = 0;
   strcpy (sim_name, "VAX 11/780");
   }
else if (strcmp(cptr, "785") == 0) {
   sys_model = 1;
   strcpy (sim_name, "VAX 11/785");
   }
else
   return SCPE_ARG;
return SCPE_OK;
}

t_stat cpu_print_model (FILE *st)
{
fprintf (st, "VAX 11/%s", (sys_model ? "785" : "780"));
return SCPE_OK;
}

t_stat cpu_model_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Initial memory size is 8MB.\n\n");
fprintf (st, "The simulator is booted with the BOOT command:\n\n");
fprintf (st, "   sim> BO{OT} <device>{/R5:flags}\n\n");
fprintf (st, "where <device> is one of:\n\n");
fprintf (st, "   RPn        to boot from rpn\n");
fprintf (st, "   HKn        to boot from hkn\n");
fprintf (st, "   RLn        to boot from rln\n");
fprintf (st, "   RQn        to boot from rqn\n");
fprintf (st, "   RQBn       to boot from rqbn\n");
fprintf (st, "   RQCn       to boot from rqcn\n");
fprintf (st, "   RQDn       to boot from rqdn\n\n");
return SCPE_OK;
}
