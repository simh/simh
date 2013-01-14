/* vax860_abus.c: VAX 8600 A-Bus

   Copyright (c) 2011-2012, Matt Burke
   This module incorporates code from SimH, Copyright (c) 2004-2008, Robert M Supnik

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   abus                 bus controller

   26-Dec-2012  MB      First Version
*/

#include "vax_defs.h"

#ifdef DONT_USE_INTERNAL_ROM
#define BOOT_CODE_FILENAME "vmb.exe"
#else /* !DONT_USE_INTERNAL_ROM */
#include "vax_vmb_exe.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#endif /* DONT_USE_INTERNAL_ROM */

/* SBIA registers */

#define SBIER_TMO       0x00001000                      /* timeout */
#define SBIER_STA       0x00000C00                      /* timeout status (0) */
#define SBIER_CNF       0x00000100                      /* error confirm */
#define SBIER_MULT      0x00000004                      /* multiple errors */
#define SBIER_TMOW1C    (SBIER_TMO|SBIER_STA|SBIER_CNF|SBIER_MULT)

/* PAMM */

#define PAMM_IOA0       0x18                            /* I/O adapter 0 */
#define PAMM_IOA1       0x19                            /* I/O adapter 1 */
#define PAMM_IOA2       0x1A                            /* I/O adapter 2 */
#define PAMM_IOA3       0x1B                            /* I/O adapter 3 */
#define PAMM_NXM        0x1F                            /* Non-existant address */

#define PAMACC_ADDR     0x3FF00000                      /* PAMM address */
#define PAMACC_CODE     0x0000001F                      /* Configuration code */

#define PAMLOC_ADDR     0x3FF00000                      /* PAMM address */

/* MBOX registers */

#define MSTAT1_V_CYC    26                              /* MBOX cycle type */
#define MSTAT1_M_CYC    0xF
#define  MSTAT1_CPRD    0xE                             /* CP read */

#define MSTAT2_NXM      0x00000008                      /* CP NXM */

#define MERG_V_MME      8                               /* Mem mgmt en */

#define MDCTL_RW        0x00006F0F                      /* MBOX data control */

/* EBOX registers */

#define EBCS_MFTL       0x00008000                      /* MBOX fatal error */

#define EHMSTS_PROCA    0x00020000                      /* Process abort */

#define EHSR_VMSE       0x00000020                      /* VMS entered */

/* VAX 8600 boot device definitions */

struct boot_dev {
    char                *name;
    int32               code;
    int32               let;
    };

uint32 nexus_req[NEXUS_HLVL];                           /* nexus int req */
uint32 pamloc = 0;
uint32 cswp = 0;
uint32 ehsr = 0;
uint32 mdctl = 0;
int32 sys_model = 0;
char cpu_boot_cmd[CBUFSIZE]  = { 0 };                   /* boot command */

static struct boot_dev boot_tab[] = {
    { "RP", BOOT_MB, 0 },
    { "HK", BOOT_HK, 0 },
    { "RL", BOOT_RL, 0 },
    { "RQ", BOOT_UDA, 1 << 24 },
    { "RQB", BOOT_UDA, 1 << 24 },
    { "RQC", BOOT_UDA, 1 << 24 },
    { "RQD", BOOT_UDA, 1 << 24 },
    { "TQ", BOOT_TK, 1 << 24 },
    { "CS", BOOT_CS, 0 },
    { NULL }
    };

extern int32 R[16];
extern int32 PSL;
extern int32 ASTLVL, SISR;
extern int32 mapen, pme, trpirq;
extern int32 in_ie;
extern int32 mchk_va, mchk_ref;
extern int32 crd_err, mem_err, hlt_pin;
extern int32 tmr_int, tti_int, tto_int, csi_int;
extern uint32 sbi_er;
extern jmp_buf save_env;
extern int32 p1;
extern int32 fault_PC;                                  /* fault PC */
extern UNIT cpu_unit;

void uba_eval_int (void);
t_stat abus_reset (DEVICE *dptr);
t_stat vax860_boot (int32 flag, char *ptr);
t_stat vax860_boot_parse (int32 flag, char *ptr);
t_stat cpu_boot (int32 unitno, DEVICE *dptr);

extern t_stat (*nexusR[NEXUS_NUM])(int32 *dat, int32 ad, int32 md);
extern t_stat (*nexusW[NEXUS_NUM])(int32 dat, int32 ad, int32 md);
extern int32 intexc (int32 vec, int32 cc, int32 ipl, int ei);
extern int32 iccs_rd (void);
extern int32 nicr_rd (void);
extern int32 icr_rd (t_bool interp);
extern int32 todr_rd (void);
extern int32 rxcs_rd (void);
extern int32 rxdb_rd (void);
extern int32 txcs_rd (void);
extern int32 stxcs_rd (void);
extern int32 stxdb_rd (void);
extern void iccs_wr (int32 dat);
extern void nicr_wr (int32 dat);
extern void todr_wr (int32 dat);
extern void rxcs_wr (int32 dat);
extern void txcs_wr (int32 dat);
extern void txdb_wr (int32 dat);
extern void stxcs_wr (int32 data);
extern void stxdb_wr (int32 data);
extern void init_mbus_tab (void);
extern void init_ubus_tab (void);
extern void init_nexus_tab (void);
extern t_stat build_mbus_tab (DEVICE *dptr, DIB *dibp);
extern t_stat build_ubus_tab (DEVICE *dptr, DIB *dibp);
extern t_stat build_nexus_tab (DEVICE *dptr, DIB *dibp);
extern void sbi_set_tmo (int32 pa);
extern int32 sbia_rd (int32 pa, int32 lnt);
extern void sbia_wr (int32 pa, int32 val, int32 lnt);
extern t_stat sbi_rd (int32 pa, int32 *val, int32 lnt);
extern t_stat sbi_wr (int32 pa, int32 val, int32 lnt);

/* ABUS data structures

   abus_dev      A-Bus device descriptor
   abus_unit     A-Bus unit
   abus_reg      A-Bus register list
*/

UNIT abus_unit = { UDATA (NULL, 0, 0) };

REG abus_reg[] = {
    { NULL }
    };

DEVICE abus_dev = {
    "ABUS", &abus_unit, abus_reg, NULL,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &abus_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* Special boot command, overrides regular boot */

CTAB vax860_cmd[] = {
    { "BOOT", &vax860_boot, RU_BOOT,
      "bo{ot} <device>{/R5:flg} boot device\n", &run_cmd_message },
    { NULL }
    };

/* The VAX 8600 has three sources of interrupts

   - internal device interrupts (CPU, console, clock)
   - nexus interupts (e.g. MBA, UBA)
   - external device interrupts (Unibus)

   Internal devices vector to fixed SCB locations.

   Nexus interrupts vector to an SCB location based on this
   formula: SCB_NEXUS + ((IPL - 0x14) * 0x40) + (TR# * 0x4)

   External device interrupts do not vector directly.
   Instead, the interrupt handler for a given UBA IPL
   reads a vector register that contains the Unibus vector
   for that IPL. */

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
if ((ipl < IPL_TTINT) && (tti_int || tto_int || csi_int)) /* console int */
    return IPL_TTINT;
if (ipl >= IPL_SMAX)                                    /* ipl >= sw max? */
    return 0;
if ((t = SISR & sw_int_mask[ipl]) == 0)
    return 0;                                           /* eligible req */
for (i = IPL_SMAX; i > ipl; i--) {                      /* check swre int */
    if ((t >> i) & 1)                                   /* req != 0? int */
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
    if (csi_int) {                                      /* console storage? */
        csi_int = 0;                                    /* clear req */
        return SCB_CSI;                                 /* return vector */
        }
    }
return 0;
}

/* Used by CPU */

void rom_wr_B (int32 pa, int32 val)
{
return;
}

/* Read 8600 specific IPR's */

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
        val = icr_rd (FALSE);
        break;

    case MT_TODR:                                       /* TODR */
        val = todr_rd ();
        break;

    case MT_ACCS:                                       /* ACCS (not impl) */
        val = 0;
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

    case MT_SID:                                        /* SID */
        if (sys_model)
            val = VAX860_SID | VAX865_TYP | VAX860_PLANT | VAX860_SN;
        else
            val = VAX860_SID | VAX860_TYP | VAX860_PLANT | VAX860_SN;
        break;

	case MT_PAMACC:                                     /* PAMACC */
        if (ADDR_IS_REG (pamloc))
            val = PAMM_IOA0;                            /* SBIA */
        else if (ADDR_IS_MEM (pamloc)) {
            if (MEMSIZE < MAXMEMSIZE)
                val = (pamloc >> 23);                   /* 4MB Boards */
            else
                val = (pamloc >> 25);                   /* 16MB Boards */
            }
		else val = PAMM_NXM;                            /* NXM */
        val = val | (pamloc & PAMACC_ADDR);
		break;

	case MT_PAMLOC:                                     /* PAMLOC */
		val = pamloc & PAMLOC_ADDR;
		break;

	case MT_MDCTL:                                      /* MDCTL */
		val = mdctl & MDCTL_RW;

	case MT_EHSR:                                       /* EHSR */
		val = ehsr & EHSR_VMSE;
		break;
        
	case MT_CSWP:                                       /* CSWP */
        val = cswp & 0xF;
		break;

	case MT_MERG:                                       /* MERG */
        val = 0;
		break;

    case MT_STXCS:                                      /* STXCS */
       val = stxcs_rd ();
       break;

    case MT_STXDB:                                      /* STXDB */
       val = stxdb_rd ();
       break;

    default:
        RSVD_OPND_FAULT;
        }

return val;
}

/* Write 8600 specific IPR's */

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

    case MT_RXCS:                                       /* RXCS */
        rxcs_wr (val);
        break;

    case MT_TXCS:                                       /* TXCS */
        txcs_wr (val);
        break;

    case MT_TXDB:                                       /* TXDB */
        txdb_wr (val);
		break;

	case MT_PAMACC:                                     /* PAMACC (not impl) */
		break;

	case MT_PAMLOC:                                     /* PAMLOC */
		pamloc = val & PAMLOC_ADDR;
		break;

	case MT_MDCTL:                                      /* MDCTL */
		mdctl = val & MDCTL_RW;
		break;

	case MT_EHSR:                                       /* EHSR */
        ehsr = val & EHSR_VMSE;
		break;

	case MT_CSWP:                                       /* CSWP */
        cswp = val & 0xF;
		break;
        
	case MT_MERG:                                       /* MERG (not impl) */
		break;

    case MT_CRBT:                                       /* CRBT (not impl) */
        break;

    case MT_STXCS:                                      /* STXCS */
	    stxcs_wr (val);
        break;

    case MT_STXDB:                                      /* STXDB */
	    stxdb_wr (val);
        break;

	default:
        RSVD_OPND_FAULT;
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

int32 ReadReg (int32 pa, int32 lnt)
{
int32 val;

if (ADDR_IS_SBIA (pa)) return sbia_rd (pa, lnt);        /* SBI adapter space? */
if (ADDR_IS_REG (pa)) {                                 /* reg space? */
    if (sbi_rd (pa, &val, lnt) == SCPE_OK)
        return val;
    }
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

void WriteReg (int32 pa, int32 val, int32 lnt)
{
if (ADDR_IS_SBIA (pa)) {                                /* SBI adapter space? */
	sbia_wr (pa, val, lnt);
    SET_IRQL;
    return;
}
if (ADDR_IS_REG (pa)) {                                 /* reg space? */
    if (sbi_wr (pa, val, lnt) == SCPE_OK)
        return;
    }
mem_err = 1;                                            /* interrupt */
eval_int ();
return;
}

/* Machine check */

int32 machine_check (int32 p1, int32 opc, int32 cc, int32 delta)
{
int32 acc;
int32 mstat1, mstat2, mear, ebcs, merg, ehmsts;

mstat1 = (MSTAT1_CPRD << MSTAT1_V_CYC);                 /* MBOX Status 1 */
mstat2 = MSTAT2_NXM;                                    /* MBOX Status 2 */
mear = mchk_va;                                         /* Memory error address */
merg = (mchk_ref << MERG_V_MME);                        /* MBOX error generation word */
ebcs = EBCS_MFTL;                                       /* EBOX control/status */
ehmsts = EHMSTS_PROCA;                                  /* Error handling microcode status */

cc = intexc (SCB_MCHK, cc, 0, IE_SVE);                  /* take exception */
acc = ACC_MASK (KERN);                                  /* in kernel mode */
in_ie = 1;
SP = SP - 92;                                           /* push 25 words */
Write (SP, 88, L_LONG, WA);                             /* # bytes */
Write (SP + 4, ehmsts, L_LONG, WA);                     /* EHM.STS */
Write (SP + 8, 0, L_LONG, WA);                          /* EVMQSAV */
Write (SP + 12, ebcs, L_LONG, WA);                      /* EBCS */
Write (SP + 16, 0, L_LONG, WA);                         /* EDPSR */
Write (SP + 20, 0, L_LONG, WA);                         /* CSLINT */
Write (SP + 24, 0, L_LONG, WA);                         /* IBESR */
Write (SP + 28, 0, L_LONG, WA);                         /* EBXWD1 */
Write (SP + 32, 0, L_LONG, WA);                         /* EBXWD2 */
Write (SP + 36, 0, L_LONG, WA);                         /* IVASAV */
Write (SP + 40, 0, L_LONG, WA);                         /* VIBASAV */
Write (SP + 44, 0, L_LONG, WA);                         /* ESASAV */
Write (SP + 48, 0, L_LONG, WA);                         /* ISASAV */
Write (SP + 52, 0, L_LONG, WA);                         /* CPC */
Write (SP + 56, mstat1, L_LONG, WA);                    /* MSTAT1 */
Write (SP + 60, mstat2, L_LONG, WA);                    /* MSTAT2 */
Write (SP + 64, 0, L_LONG, WA);                         /* MDECC */
Write (SP + 68, merg, L_LONG, WA);                      /* MERG */
Write (SP + 72, 0, L_LONG, WA);                         /* CSHCTL */
Write (SP + 76, mear, L_LONG, WA);                      /* MEAR */
Write (SP + 80, 0, L_LONG, WA);                         /* MEDR */
Write (SP + 84, 0, L_LONG, WA);                         /* FBXERR */
Write (SP + 88, 0, L_LONG, WA);                         /* CSES */
in_ie = 0;
sbi_er = sbi_er & ~SBIER_TMOW1C;                        /* clr SBIER<tmo> etc */
ehsr = ehsr | EHSR_VMSE;                                /* VMS entered */
return cc;
}

/* Console entry */

int32 con_halt (int32 code, int32 cc)
{
if ((cpu_boot_cmd[0] == 0) ||                           /* saved boot cmd? */
    (vax860_boot_parse (0, cpu_boot_cmd) != SCPE_OK) || /* reparse the boot cmd */ 
    (reset_all (0) != SCPE_OK) ||                       /* reset the world */
    (cpu_boot (0, NULL) != SCPE_OK))                    /* set up boot code */
    ABORT (STOP_BOOT);                                  /* any error? */
printf ("Rebooting...\n");
if (sim_log)
    fprintf (sim_log, "Rebooting...\n");
return cc;
}

/* Special boot command - linked into SCP by initial reset

   Syntax: BOOT <device>{/R5:val}

   Sets up R0-R5, calls SCP boot processor with effective BOOT CPU
*/

t_stat vax860_boot (int32 flag, char *ptr)
{
t_stat r;

r = vax860_boot_parse (flag, ptr);                      /* parse the boot cmd */
if (r != SCPE_OK)                                       /* error? */
    return r;
strncpy (cpu_boot_cmd, ptr, CBUFSIZE);                  /* save for reboot */
return run_cmd (flag, "CPU");
}

/* Parse boot command, set up registers - also used on reset */

t_stat vax860_boot_parse (int32 flag, char *ptr)
{
char gbuf[CBUFSIZE];
char *slptr, *regptr;
int32 i, r5v, unitno;
DEVICE *dptr;
UNIT *uptr;
DIB *dibp;
uint32 ba;
t_stat r;

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

/* A-Bus reset */

t_stat abus_reset (DEVICE *dptr)
{
sim_vm_cmd = vax860_cmd;
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

t_stat cpu_set_model (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (cptr == NULL) return SCPE_ARG;
if (strcmp(cptr, "8600") == 0)
   sys_model = 0;
else if (strcmp(cptr, "8650") == 0)
   sys_model = 1;
else
   return SCPE_ARG;
return SCPE_OK;
}

t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "model=VAX %s", (sys_model ? "8650" : "8600"));
return SCPE_OK;
}
