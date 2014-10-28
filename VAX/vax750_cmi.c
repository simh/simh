/* vax750_cmi.c: VAX 11/750 CMI

   Copyright (c) 2010-2011, Matt Burke
   This module incorporates code from SimH, Copyright (c) 2004-2011, Robert M Supnik

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   This module contains the VAX 11/750 system-specific registers and devices.

   cmi                  bus controller

   21-Oct-2012  MB      First Version
*/

#include "vax_defs.h"

#ifdef DONT_USE_INTERNAL_ROM
#define BOOT_CODE_FILENAME "vmb.exe"
#else /* !DONT_USE_INTERNAL_ROM */
#include "vax_vmb_exe.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#endif /* DONT_USE_INTERNAL_ROM */

/* 11/750 specific IPRs */

#define CMIERR_CRD            0x00000001
#define CMIERR_LEB            0x00000002
#define CMIERR_RDS            0x00000004
#define CMIERR_ME             0x00000008
#define CMIERR_TBH            0x00000010
#define CMIERR_TBG0DE         0x00000100
#define CMIERR_TBG1DE         0x00000200
#define CMIERR_TBG0TE         0x00000400
#define CMIERR_TBG1TE         0x00000800
#define CMIERR_V_MODE         16
#define CMIERR_M_MODE         0x3
#define CMIERR_MODE           (CMIERR_M_MODE << CMIERR_V_MODE)
#define CMIERR_REF            0x00040000
#define CMIERR_RM             0x00080000
#define CMIERR_EN             0x00100000

/* System registers */

/* VAX-11/750 boot device definitions */

struct boot_dev {
    char                *name;
    int32               code;
    int32               let;
    };

uint32 nexus_req[NEXUS_HLVL];                           /* nexus int req */
uint32 cmi_err = 0;
uint32 cmi_cadr = 0;
char cpu_boot_cmd[CBUFSIZE]  = { 0 };                   /* boot command */
int32 sys_model = 0;

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
    { "TQ", BOOT_TK, 1 << 24 },
    { "TD", BOOT_TD, 0 },
    { NULL }
    };

extern int32 R[16];
extern int32 PSL;
extern int32 ASTLVL, SISR;
extern int32 mapen, pme, trpirq;
extern int32 in_ie;
extern int32 mchk_va, mchk_ref;
extern int32 crd_err, mem_err, hlt_pin;
extern int32 tmr_int, tti_int, tto_int, csi_int, cso_int;
extern jmp_buf save_env;
extern int32 p1;

t_stat cmi_reset (DEVICE *dptr);
char *cmi_description (DEVICE *dptr);
void cmi_set_tmo (void);
t_stat vax750_boot (int32 flag, char *ptr);
t_stat vax750_boot_parse (int32 flag, char *ptr);
t_stat cpu_boot (int32 unitno, DEVICE *dptr);

extern int32 intexc (int32 vec, int32 cc, int32 ipl, int ei);
extern int32 iccs_rd (void);
extern int32 nicr_rd (void);
extern int32 icr_rd (t_bool interp);
extern int32 todr_rd (void);
extern int32 rxcs_rd (void);
extern int32 rxdb_rd (void);
extern int32 txcs_rd (void);
extern int32 csrs_rd (void);
extern int32 csrd_rd (void);
extern int32 csts_rd (void);
extern void iccs_wr (int32 dat);
extern void nicr_wr (int32 dat);
extern void todr_wr (int32 dat);
extern void rxcs_wr (int32 dat);
extern void txcs_wr (int32 dat);
extern void txdb_wr (int32 dat);
extern void csrs_wr (int32 dat);
extern void csts_wr (int32 dat);
extern void cstd_wr (int32 dat);
extern void init_mbus_tab (void);
extern void init_ubus_tab (void);
extern t_stat build_mbus_tab (DEVICE *dptr, DIB *dibp);
extern t_stat build_ubus_tab (DEVICE *dptr, DIB *dibp);
extern void uba_eval_int (void);
extern int32 uba_get_ubvector (int32 lvl);
extern void uba_ioreset (void);

/* CMI data structures

   cmi_dev      CMI device descriptor
   cmi_unit     CMI unit
   cmi_reg      CMI register list
*/

UNIT cmi_unit = { UDATA (NULL, 0, 0) };

REG cmi_reg[] = {
    { HRDATA (NREQ14, nexus_req[0], 16) },
    { HRDATA (NREQ15, nexus_req[1], 16) },
    { HRDATA (NREQ16, nexus_req[2], 16) },
    { HRDATA (NREQ17, nexus_req[3], 16) },
    { HRDATA (CMIERR, cmi_err, 32) },
    { BRDATA (BOOTCMD, cpu_boot_cmd, 16, 8, CBUFSIZE), REG_HRO },
    { NULL }
    };

DEVICE cmi_dev = {
    "CMI", &cmi_unit, cmi_reg, NULL,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &cmi_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &cmi_description 
    };

/* Special boot command, overrides regular boot */

CTAB vax750_cmd[] = {
    { "BOOT", &vax750_boot, RU_BOOT,
      "bo{ot} <device>{/R5:flg} boot device\n"
      "                         type HELP CPU to see bootable devices\n", NULL, &run_cmd_message },
    { NULL }
    };

/* The VAX 11/750 has three sources of interrupts

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

   Find highest priority vectorable interrupt */

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
if ((ipl < IPL_TTINT) && (tti_int || tto_int || csi_int || cso_int))          /* console int */
    return IPL_TTINT;
if (ipl >= IPL_SMAX)                                    /* ipl >= sw max? */
    return 0;
if ((t = SISR & sw_int_mask[ipl]) == 0)
    return 0;       /* eligible req */
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
    if (nexus_req[l] & (1u << TR_UBA)) {                /* unibus int? */
        nexus_req[l] = nexus_req[l] & ~(1u << TR_UBA);
        return uba_get_ubvector(l);
        }
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
    if (csi_int) {                                      /* input? */
        csi_int = 0;                                    /* clear req */
        return SCB_CSI;                                 /* return vector */
        }
    if (cso_int) {                                      /* output? */
        cso_int = 0;                                    /* clear req */
        return SCB_CSO;                                 /* return vector */
        }
    }
return 0;
}

/* Read 750-specific IPR's */

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

    case MT_CADR:                                       /* CADR */
        val = cmi_cadr;
        break;

    case MT_CAER:                                       /* CAER (not impl) */
        val = 0;
        break;

    case MT_MCESR:                                      /* MCESR (not impl) */
        val = 0;
        break;

    case MT_CMIE:                                       /* CMIE */
        val = cmi_err;
        break;

    case MT_CSRS:                                       /* CSRS */
        val = csrs_rd ();
        break;
        
    case MT_CSRD:                                       /* CSRD */
        val = csrd_rd ();
        break;
        
    case MT_CSTS:                                       /* CSTS */
        val = csts_rd ();
        break;

    case MT_TBDR:                                       /* TBDR */
        val = 0;
        break;

    case MT_SID:                                        /* SID */
        val = VAX750_SID | VAX750_MICRO | VAX750_HWREV;
        break;

    default:
        RSVD_OPND_FAULT;
        }

return val;
}

/* Write 750-specific IPR's */

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

    case MT_CADR:                                       /* CADR */
        cmi_cadr = (val & 0x1);
        break;

    case MT_CAER:                                       /* CAER (not impl) */
        break;

    case MT_MCESR:                                      /* MCESR (not impl) */
        break;

    case MT_IORESET:                                    /* IORESET */
        uba_ioreset ();
        break;

    case MT_CSRS:                                       /* CSRS */
        csrs_wr (val);
        break;
        
    case MT_CSTS:                                       /* CSTS */
        csts_wr (val);
        break;
        
    case MT_CSTD:                                       /* CSTD */
        cstd_wr (val);
        break;

    case MT_TBDR:                                       /* TBDR */
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
int32 nexus, val;

if (ADDR_IS_REG (pa)) {                                 /* reg space? */
    nexus = NEXUS_GETNEX (pa);                          /* get nexus */
    if (nexusR[nexus] &&                                /* valid? */
        (nexusR[nexus] (&val, pa, lnt) == SCPE_OK)) {
        SET_IRQL;
        return val;
        }
    }
cmi_set_tmo ();                                         /* timeout */
MACH_CHECK (MCHK_BPE);                                  /* machine check */
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
int32 nexus;

if (ADDR_IS_REG (pa)) {                                 /* reg space? */
    nexus = NEXUS_GETNEX (pa);                          /* get nexus */
    if (nexusW[nexus] &&                                /* valid? */
        (nexusW[nexus] (val, pa, lnt) == SCPE_OK)) {
        SET_IRQL;
        return;
        }
    }
cmi_set_tmo ();                                         /* timeout */
mem_err = 1;                                            /* interrupt */
SET_IRQL;
return;
}

/* Set CMI timeout */

void cmi_set_tmo ()
{
if ((cmi_err & CMIERR_ME) == 0) {                       /* not yet set? */
    if (mchk_ref == REF_V)                              /* virt? add mode */
        cmi_err |= CMIERR_REF | (PSL_GETCUR (PSL) << CMIERR_V_MODE);
    cmi_err |= CMIERR_ME;                               /* set tmo flag */
    }
else cmi_err |= CMIERR_LEB;                             /* yes, multiple */
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
int32 acc;
if (p1 == MCHK_BPE)                                     /* bus error? */
    cc = intexc (SCB_MCHK, cc, 0, IE_EXC);              /* take normal exception */
else
    cc = intexc (SCB_MCHK, cc, 0, IE_SVE);              /* take severe exception */
acc = ACC_MASK (KERN);                                  /* in kernel mode */
in_ie = 1;
SP = SP - 44;                                           /* push 11 words */
Write (SP, 40, L_LONG, WA);                             /* # bytes */
Write (SP + 4, p1, L_LONG, WA);                         /* error code */
Write (SP + 8, mchk_va, L_LONG, WA);                    /* VA register */
Write (SP + 12, 0, L_LONG, WA);                         /* Fault PC */
Write (SP + 16, 0, L_LONG, WA);                         /* MDR */
Write (SP + 20, 0, L_LONG, WA);                         /* saved mode reg */
Write (SP + 24, 0, L_LONG, WA);                         /* read lock timeout */
Write (SP + 28, 0, L_LONG, WA);                         /* TB group parity error reg */
Write (SP + 32, 0, L_LONG, WA);                         /* cache error reg */
Write (SP + 36, cmi_err, L_LONG, WA);                   /* bus error reg */
Write (SP + 40, 0, L_LONG, WA);                         /* MCESR */
in_ie = 0;
cmi_err = cmi_err & ~CMIERR_ME;                         /* clr CMIERR<me> etc */
return cc;
}

/* Console entry - only reached if CONHALT is set (AUTORESTART is set) */

int32 con_halt (int32 code, int32 cc)
{
if ((cpu_boot_cmd[0] == 0) ||                           /* saved boot cmd? */
    (vax750_boot_parse (0, cpu_boot_cmd) != SCPE_OK) || /* reparse the boot cmd */ 
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

t_stat vax750_boot (int32 flag, char *ptr)
{
t_stat r;

r = vax750_boot_parse (flag, ptr);                      /* parse the boot cmd */
if (r != SCPE_OK) {                                     /* error? */
    if (r >= SCPE_BASE) {                               /* message available? */
        sim_printf ("%s\n", sim_error_text (r));
        r |= SCPE_NOMESSAGE;
        }
    return r;
    }
strncpy (cpu_boot_cmd, ptr, CBUFSIZE);                  /* save for reboot */
return run_cmd (flag, "CPU");
}

/* Parse boot command, set up registers - also used on reset */

t_stat vax750_boot_parse (int32 flag, char *ptr)
{
char gbuf[CBUFSIZE];
char *slptr, *regptr;
int32 i, r5v, unitno;
DEVICE *dptr;
UNIT *uptr;
DIB *dibp;
uint32 ba;
t_stat r;

if (!ptr || !*ptr)
    return SCPE_2FARG;
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
            R[1] = (NEXUSBASE + (TR_MBA0 * NEXUSSIZE));
            R[2] = unitno;
            }
        else {
            R[1] = ba;
            R[2] = (ba & UBADDRMASK);
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

/* CMI reset */

t_stat cmi_reset (DEVICE *dptr)
{
sim_vm_cmd = vax750_cmd;
cmi_err = CMIERR_EN;
cmi_cadr = 0;
return SCPE_OK;
}

char *cmi_description (DEVICE *dptr)
{
return "CPU/Memory interconnect";
}

/* Show nexus */

t_stat show_nexus (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "nexus=%d", val);
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

t_stat cpu_print_model (FILE *st)
{
fprintf (st, "VAX 11/750");
return SCPE_OK;
}

t_stat cpu_model_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "Initial memory size is 2MB.\n\n");
fprintf (st, "The simulator is booted with the BOOT command:\n\n");
fprintf (st, "   sim> BO{OT} <device>{/R5:flags}\n\n");
fprintf (st, "where <device> is one of:\n\n");
fprintf (st, "   RPn        to boot from rpn\n");
fprintf (st, "   HKn        to boot from hkn\n");
fprintf (st, "   RLn        to boot from rln\n");
fprintf (st, "   RQn        to boot from rqn\n");
fprintf (st, "   RQBn       to boot from rqbn\n");
fprintf (st, "   RQCn       to boot from rqcn\n");
fprintf (st, "   RQDn       to boot from rqdn\n");
fprintf (st, "   TQn        to boot from tqn\n");
fprintf (st, "   TDn        to boot from tdn (TU58)\n\n");
return SCPE_OK;
}
