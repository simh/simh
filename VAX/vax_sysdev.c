/* vax_sysdev.c: VAX 3900 system-specific logic

   Copyright (c) 1998-2019, Robert M Supnik

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

   This module contains the CVAX chip and VAX 3900 system-specific registers
   and devices.

   rom          bootstrap ROM (no registers)
   nvr          non-volatile ROM (no registers)
   csi          console storage input
   cso          console storage output
   cmctl        memory controller
   sysd         system devices (SSC miscellany)

   05-May-19    RMS     Removed Qbus memory space from register space
   20-Dec-13    RMS     Added unaligned register space access routines
   23-Dec-10    RMS     Added power clear call to boot routine (Mark Pizzolato)
   25-Oct-05    RMS     Automated CMCTL extended memory
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   10-Mar-05    RMS     Fixed bug in timer schedule routine (Mark Hittinger)
   30-Sep-04    RMS     Moved CADR, MSER, CONPC, CONPSL, machine_check, cpu_boot,
                         con_halt here from vax_cpu.c
                        Moved model-specific IPR's here from vax_cpu1.c
   09-Sep-04    RMS     Integrated powerup into RESET (with -p)
                        Added model-specific registers and routines from CPU
   23-Jan-04    MP      Added extended physical memory support (Mark Pizzolato)
   07-Jun-03    MP      Added calibrated delay to ROM reads (Mark Pizzolato)
                        Fixed calibration problems interval timer (Mark Pizzolato)
   12-May-03    RMS     Fixed compilation warnings from VC.Net
   23-Apr-03    RMS     Revised for 32b/64b t_addr
   19-Aug-02    RMS     Removed unused variables (David Hittner)
                        Allowed NVR to be attached to file
   30-May-02    RMS     Widened POS to 32b
   28-Feb-02    RMS     Fixed bug, missing end of table (Lars Brinkhoff)
*/

#include "vax_defs.h"

#include <math.h>

#ifdef DONT_USE_INTERNAL_ROM
#define BOOT_CODE_FILENAME "ka655x.bin"
#else /* !DONT_USE_INTERNAL_ROM */
#include "vax_ka655x_bin.h" /* Defines BOOT_CODE_FILENAME and BOOT_CODE_ARRAY, etc */
#endif /* DONT_USE_INTERNAL_ROM */

#define UNIT_V_NODELAY  (UNIT_V_UF + 0)                 /* ROM access equal to RAM access */
#define UNIT_NODELAY    (1u << UNIT_V_NODELAY)

t_stat vax_boot (int32 flag, CONST char *ptr);
int32 sys_model = 0;

/* Special boot command, overrides regular boot */

CTAB vax_cmd[] = {
    { "BOOT", &vax_boot, RU_BOOT,
      "bo{ot}                   boot simulator\n", NULL, &run_cmd_message },
    { NULL }
    };

/* Console storage control/status */

#define CSICSR_IMP      (CSR_DONE + CSR_IE)             /* console input */
#define CSICSR_RW       (CSR_IE)
#define CSOCSR_IMP      (CSR_DONE + CSR_IE)             /* console output */
#define CSOCSR_RW       (CSR_IE)

/* CMCTL configuration registers */

#define CMCNF_VLD       0x80000000                      /* addr valid */
#define CMCNF_BA        0x1FF00000                      /* base addr */
#define CMCNF_LOCK      0x00000040                      /* lock NI */
#define CMCNF_SRQ       0x00000020                      /* sig req WO */
#define CMCNF_SIG       0x0000001F                      /* signature */
#define CMCNF_RW        (CMCNF_VLD | CMCNF_BA)          /* read/write */
#define CMCNF_MASK      (CMCNF_RW | CMCNF_SIG)
#define MEM_BANK        (1 << 22)                       /* bank size 4MB */
#define MEM_SIG         (0x17)                          /* ECC, 4 x 4MB */

/* CMCTL error register */

#define CMERR_RDS       0x80000000                      /* uncorr err NI */
#define CMERR_FRQ       0x40000000                      /* 2nd RDS NI */
#define CMERR_CRD       0x20000000                      /* CRD err NI */
#define CMERR_PAG       0x1FFFFC00                      /* page addr NI */
#define CMERR_DMA       0x00000100                      /* DMA err NI */
#define CMERR_BUS       0x00000080                      /* bus err NI */
#define CMERR_SYN       0x0000007F                      /* syndrome NI */
#define CMERR_W1C       (CMERR_RDS | CMERR_FRQ | CMERR_CRD | \
                         CMERR_DMA | CMERR_BUS)

/* CMCTL control/status register */

#define CMCSR_PMI       0x00002000                      /* PMI speed NI */
#define CMCSR_CRD       0x00001000                      /* enb CRD int NI */
#define CMCSR_FRF       0x00000800                      /* force ref WONI */
#define CMCSR_DET       0x00000400                      /* dis err NI */
#define CMCSR_FDT       0x00000200                      /* fast diag NI */
#define CMCSR_DCM       0x00000080                      /* diag mode NI */
#define CMCSR_SYN       0x0000007F                      /* syndrome NI */
#define CMCSR_MASK      (CMCSR_PMI | CMCSR_CRD | CMCSR_DET | \
                         CMCSR_FDT | CMCSR_DCM | CMCSR_SYN)

/* KA655 boot/diagnostic register */

#define BDR_BRKENB      0x00000080                      /* break enable */

/* KA655 cache control register */

#define CACR_DRO        0x00FFFF00                      /* diag bits RO */
#define CACR_V_DPAR     24                              /* data parity */
#define CACR_FIXED      0x00000040                      /* fixed bits */
#define CACR_CPE        0x00000020                      /* parity err W1C */
#define CACR_CEN        0x00000010                      /* enable */                    
#define CACR_DPE        0x00000004                      /* disable par NI */
#define CACR_WWP        0x00000002                      /* write wrong par NI */
#define CACR_DIAG       0x00000001                      /* diag mode */
#define CACR_W1C        (CACR_CPE)
#define CACR_RW         (CACR_CEN | CACR_DPE | CACR_WWP | CACR_DIAG)

/* SSC base register */

#define SSCBASE_MBO     0x20000000                      /* must be one */
#define SSCBASE_RW      0x1FFFFC00                      /* base address */

/* SSC configuration register */

#define SSCCNF_BLO      0x80000000                      /* batt low W1C */
#define SSCCNF_IVD      0x08000000                      /* int dsbl NI */
#define SSCCNF_IPL      0x03000000                      /* int IPL NI */
#define SSCCNF_ROM      0x00F70000                      /* ROM param NI */
#define SSCCNF_CTLP     0x00008000                      /* ctrl P enb */
#define SSCCNF_BAUD     0x00007700                      /* baud rates NI */
#define SSCCNF_ADS      0x00000077                      /* addr strb NI */
#define SSCCNF_W1C      SSCCNF_BLO
#define SSCCNF_RW       0x0BF7F777

static BITFIELD ssc_cnf_bits[] = {
    BITF(ADS1,3),                       /* addr strb-1 NI */
    BITNC,                              /* unused */
    BITF(ADS2,3),                       /* addr strb-2 NI */
    BITNC,                              /* unused */
    BITF(BAUD1,3),                      /* baud rate-1 NI */
    BITNC,                              /* unused */
    BITF(BAUD2,3),                      /* baud rate-2 NI */
    BIT(CTLP),                          /* ctrl P enb */
    BITF(ROM,8),                        /* ROM param NI */
    BITF(IPL,2),                        /* int IPL NI */
    BITNC,                              /* unused */
    BIT(IVD),                           /* int dsbl NI */
    BITNCF(3),                          /* unused */
    BIT(BLO),                           /* batt low W1C */
    ENDBITS
};

/* SSC timeout register */

#define SSCBTO_BTO      0x80000000                      /* timeout W1C */
#define SSCBTO_RWT      0x40000000                      /* read/write W1C */
#define SSCBTO_INTV     0x00FFFFFF                      /* interval NI */
#define SSCBTO_W1C      (SSCBTO_BTO | SSCBTO_RWT)
#define SSCBTO_RW       SSCBTO_INTV

/* SSC output port */

#define SSCOTP_MASK     0x0000000F                      /* output port */

/* SSC timer control/status */

#define TMR_CSR_ERR     0x80000000                      /* error W1C */
#define TMR_CSR_DON     0x00000080                      /* done W1C */
#define TMR_CSR_IE      0x00000040                      /* int enb */
#define TMR_CSR_SGL     0x00000020                      /* single WO */
#define TMR_CSR_XFR     0x00000010                      /* xfer WO */
#define TMR_CSR_STP     0x00000004                      /* stop */
#define TMR_CSR_RUN     0x00000001                      /* run */
#define TMR_CSR_W1C     (TMR_CSR_ERR | TMR_CSR_DON)
#define TMR_CSR_RW      (TMR_CSR_IE | TMR_CSR_STP | TMR_CSR_RUN)

static BITFIELD tmr_csr_bits[] = {
    BIT(RUN),                           /* run */
    BITNC,                              /* unused */
    BIT(STP),                           /* stop */
    BITNC,                              /* unused */
    BIT(XFR),                           /* xfer */
    BIT(SGL),                           /* Single */
    BIT(IE),                            /* Interrupt Enable */
    BIT(DON),                           /* Xmit Ready */
    BITNCF(23),                         /* unused */
    BIT(ERR),                           /* Xmit Ready */
    ENDBITS
};


/* SSC timer intervals */

#define TMR_INC         10000U                          /* usec/interval */

/* SSC timer vector */

#define TMR_VEC_MASK    0x000003FC                      /* vector */

/* SSC address strobes */

#define SSCADS_MASK     0x3FFFFFFC                      /* match or mask */

extern UNIT clk_unit;
extern int32 MSER;
extern int32 tmr_poll;
extern DEVICE vc_dev, lk_dev, vs_dev;

uint32 *rom = NULL;                                     /* boot ROM */
uint32 *nvr = NULL;                                     /* non-volatile mem */
int32 CADR = 0;                                         /* cache disable reg */
int32 MSER = 0;                                         /* mem sys error reg */
int32 conpc, conpsl;                                    /* console reg */
int32 csi_csr = 0;                                      /* control/status */
int32 cso_csr = 0;                                      /* control/status */
int32 cmctl_reg[CMCTLSIZE >> 2] = { 0 };                /* CMCTL reg */
int32 ka_cacr = 0;                                      /* KA655 cache ctl */
int32 ka_bdr = BDR_BRKENB;                              /* KA655 boot diag */
t_bool ka_hltenab = 1;                                  /* Halt Enable / Autoboot flag */
int32 ssc_base = SSCBASE;                               /* SSC base */
int32 ssc_cnf = 0;                                      /* SSC conf */
int32 ssc_bto = 0;                                      /* SSC timeout */
int32 ssc_otp = 0;                                      /* SSC output port */
int32 tmr_csr[2] = { 0 };                               /* SSC timers */
uint32 tmr_tir[2] = { 0 };                              /* curr interval */
uint32 tmr_tnir[2] = { 0 };                             /* next interval */
int32 tmr_tivr[2] = { 0 };                              /* vector */
t_bool tmr_inst[2] = { 0 };                             /* wait instructions vs usecs */
int32 ssc_adsm[2] = { 0 };                              /* addr strobes */
int32 ssc_adsk[2] = { 0 };
int32 cdg_dat[CDASIZE >> 2];                            /* cache data */

t_stat rom_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat rom_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat rom_reset (DEVICE *dptr);
t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *rom_description (DEVICE *dptr);
t_stat nvr_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat nvr_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat nvr_reset (DEVICE *dptr);
t_stat nvr_attach (UNIT *uptr, CONST char *cptr);
t_stat nvr_detach (UNIT *uptr);
t_stat nvr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *nvr_description (DEVICE *dptr);
t_stat csi_reset (DEVICE *dptr);
const char *csi_description (DEVICE *dptr);
t_stat cso_reset (DEVICE *dptr);
t_stat cso_svc (UNIT *uptr);
const char *cso_description (DEVICE *dptr);
t_stat tmr_svc (UNIT *uptr);
t_stat sysd_reset (DEVICE *dptr);
t_stat sysd_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *sysd_description (DEVICE *dptr);

int32 rom_rd (int32 pa);
int32 nvr_rd (int32 pa);
void nvr_wr (int32 pa, int32 val, int32 lnt);
int32 csrs_rd (void);
int32 csrd_rd (void);
int32 csts_rd (void);
void csrs_wr (int32 dat);
void csts_wr (int32 dat);
void cstd_wr (int32 dat);
int32 cmctl_rd (int32 pa);
void cmctl_wr (int32 pa, int32 val, int32 lnt);
int32 ka_rd (int32 pa);
void ka_wr (int32 pa, int32 val, int32 lnt);
int32 cdg_rd (int32 pa);
void cdg_wr (int32 pa, int32 val, int32 lnt);
int32 ssc_rd (int32 pa);
void ssc_wr (int32 pa, int32 val, int32 lnt);
int32 tmr_tir_rd (int32 tmr);
void tmr_csr_wr (int32 tmr, int32 val);
int32 tmr_csr_rd (int32 tmr);
void tmr_sched (int32 tmr);
void tmr_incr (int32 tmr, uint32 inc);
int32 tmr0_inta (void);
int32 tmr1_inta (void);
int32 parity (int32 val, int32 odd);
t_stat sysd_powerup (void);

extern int32 intexc (int32 vec, int32 cc, int32 ipl, int ei);
extern int32 cqmap_rd (int32 pa);
extern void cqmap_wr (int32 pa, int32 val, int32 lnt);
extern int32 cqipc_rd (int32 pa);
extern void cqipc_wr (int32 pa, int32 val, int32 lnt);
extern int32 cqbic_rd (int32 pa);
extern void cqbic_wr (int32 pa, int32 val, int32 lnt);
extern int32 iccs_rd (void);
extern int32 todr_rd (void);
extern int32 rxcs_rd (void);
extern int32 rxdb_rd (void);
extern int32 txcs_rd (void);
extern void iccs_wr (int32 dat);
extern void todr_wr (int32 dat);
extern void rxcs_wr (int32 dat);
extern void txcs_wr (int32 dat);
extern void txdb_wr (int32 dat);
extern void ioreset_wr (int32 dat);
extern void cpu_idle (void);

/* ROM data structures

   rom_dev      ROM device descriptor
   rom_unit     ROM units
   rom_reg      ROM register list
*/

UNIT rom_unit = { UDATA (NULL, UNIT_FIX+UNIT_BINK, ROMSIZE) };

REG rom_reg[] = {
    { NULL }
    };

MTAB rom_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,     NULL, &show_mapped_addr, (void *)ROMBASE, "Display base address" },
    { UNIT_NODELAY, UNIT_NODELAY, "fast access", "NODELAY", NULL, NULL, NULL, "Disable calibrated delay - ROM runs like RAM" },
    { UNIT_NODELAY, 0, "1usec calibrated access", "DELAY",  NULL, NULL, NULL, "Enable calibrated ROM delay - ROM runs slowly" },
    { 0 }
    };

DEVICE rom_dev = {
    "ROM", &rom_unit, rom_reg, rom_mod,
    1, 16, ROMAWIDTH, 4, 16, 32,
    &rom_ex, &rom_dep, &rom_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, &rom_help, NULL, NULL, 
    &rom_description
    };

/* NVR data structures

   nvr_dev      NVR device descriptor
   nvr_unit     NVR units
   nvr_reg      NVR register list
*/

UNIT nvr_unit =
    { UDATA (NULL, UNIT_FIX+UNIT_BINK, NVRSIZE) };

REG nvr_reg[] = {
    { NULL }
    };

MTAB nvr_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,     NULL, &show_mapped_addr, (void *)NVRBASE, "Display base address" },
    { 0 }
    };

DEVICE nvr_dev = {
    "NVR", &nvr_unit, nvr_reg, nvr_mod,
    1, 16, NVRAWIDTH, 4, 16, 32,
    &nvr_ex, &nvr_dep, &nvr_reset,
    NULL, &nvr_attach, &nvr_detach,
    NULL, 0, 0, NULL, NULL, NULL, &nvr_help, NULL, NULL, 
    &nvr_description
    };

/* CSI data structures

   csi_dev      CSI device descriptor
   csi_unit     CSI unit descriptor
   csi_reg      CSI register list
*/

DIB csi_dib = { 0, 0, NULL, NULL, 1, IVCL (CSI), SCB_CSI, { NULL } };

UNIT csi_unit = { UDATA (NULL, 0, 0), KBD_POLL_WAIT };

REG csi_reg[] = {
    { ORDATAD (BUF,  csi_unit.buf,             8, "last data item processed") },
    { ORDATAD (CSR,  csi_csr,                 16, "control/status register") },
    { FLDATAD (INT,  int_req[IPL_CSI], INT_V_CSI, "interrupt pending flag") },
    { FLDATAD (DONE, csi_csr,         CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (ERR,  csi_csr,          CSR_V_ERR, "error flag (CSR<15>)") },
    { FLDATAD (IE,   csi_csr,           CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (POS,  csi_unit.pos,            32, "number of characters input"), PV_LEFT },
    { DRDATAD (TIME, csi_unit.wait,           24, "input polling interval"), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB csi_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,     NULL, &show_vec, NULL, "Display interrupt vector" },
    { 0 }
    };

DEVICE csi_dev = {
    "CSI", &csi_unit, csi_reg, csi_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &csi_reset,
    NULL, NULL, NULL,
    &csi_dib, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL
    };

/* CSO data structures

   cso_dev      CSO device descriptor
   cso_unit     CSO unit descriptor
   cso_reg      CSO register list
*/

DIB cso_dib = { 0, 0, NULL, NULL, 1, IVCL (CSO), SCB_CSO, { NULL } };

UNIT cso_unit = { UDATA (&cso_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG cso_reg[] = {
    { ORDATAD (BUF,     cso_unit.buf,          8, "last data item processed") },
    { ORDATAD (CSR,          cso_csr,         16, "control/status register") },
    { FLDATAD (INT, int_req[IPL_CSO],  INT_V_CSO, "interrupt pending flag") },
    { FLDATAD (ERR,          cso_csr,  CSR_V_ERR, "error flag (CSR<15>)") },
    { FLDATAD (DONE,         cso_csr, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE,           cso_csr,   CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (POS,     cso_unit.pos,         32, "number of characters output"), PV_LEFT },
    { DRDATAD (TIME,   cso_unit.wait,         24, "time from I/O initiation to interrupt"), PV_LEFT },
    { NULL }
    };

MTAB cso_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,     NULL, &show_vec, NULL, "Display interrupt vector" },
    { 0 }
    };

DEVICE cso_dev = {
    "CSO", &cso_unit, cso_reg, cso_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &cso_reset,
    NULL, NULL, NULL,
    &cso_dib, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL
    };

/* SYSD data structures

   sysd_dev     SYSD device descriptor
   sysd_unit    SYSD units
   sysd_reg     SYSD register list
*/

DIB sysd_dib[] = {
    {0, 0, NULL, NULL,
     2, IVCL (TMR0), 0, { &tmr0_inta, &tmr1_inta } }
    };

UNIT sysd_unit[] = {
    { UDATA (&tmr_svc, 0, 0) },
    { UDATA (&tmr_svc, 0, 0) }
    };

REG sysd_reg[] = {
    { HRDATAD (CADR,   CADR,         8, "cache disable register") },
    { HRDATAD (MSER,   MSER,         8, "memory system error register") },
    { HRDATAD (CONPC,  conpc,       32, "PC at console halt") },
    { HRDATAD (CONPSL, conpsl,      32, "PSL at console halt") },
    { BRDATAD (CMCSR,  cmctl_reg, 16, 32, CMCTLSIZE >> 2, "CMCTL control and status registers") },
    { HRDATAD (CACR,   ka_cacr,      8, "second-level cache control register") },
    { HRDATAD (BDR,    ka_bdr,       8, "front panel jumper register") },
    { HRDATAD (BASE,   ssc_base,    29, "SSC base address register") },
    { HRDATADF (CNF,   ssc_cnf,     32, "SSC configuration register", ssc_cnf_bits) },
    { HRDATAD (BTO,    ssc_bto,     32, "SSC bus timeout register") },
    { HRDATAD (OTP,    ssc_otp,      4, "SSC output port") },
    { HRDATADF (TCSR0, tmr_csr[0],  32, "SSC timer 0 control/status register", tmr_csr_bits) },
    { HRDATAD (TIR0,   tmr_tir[0],  32, "SSC timer 0 interval register") },
    { HRDATAD (TNIR0,  tmr_tnir[0], 32, "SSC timer 0 next interval register") },
    { HRDATAD (TIVEC0, tmr_tivr[0],  9, "SSC timer 0 interrupt vector register") },
    { FLDATAD (TINST0, tmr_inst[0],  0, "SSC timer 0 last wait instructions") },
    { HRDATADF (TCSR1, tmr_csr[1],  32, "SSC timer 1 control/status register", tmr_csr_bits) },
    { HRDATAD (TIR1,   tmr_tir[1],  32, "SSC timer 1 interval register") },
    { HRDATAD (TNIR1,  tmr_tnir[1], 32, "SSC timer 1 next interval register") },
    { HRDATAD (TIVEC1, tmr_tivr[1],  9, "SSC timer 1 interrupt vector register") },
    { FLDATAD (TINST1, tmr_inst[1],  0, "SSC timer 1 last wait instructions") },
    { HRDATAD (ADSM0,  ssc_adsm[0], 32, "SSC address match 0 address") },
    { HRDATAD (ADSK0,  ssc_adsk[0], 32, "SSC address match 0 mask") },
    { HRDATAD (ADSM1,  ssc_adsm[1], 32, "SSC address match 1 address") },
    { HRDATAD (ADSK1,  ssc_adsk[1], 32, "SSC address match 1 mask") },
    { BRDATAD (CDGDAT, cdg_dat, 16, 32, CDASIZE >> 2, "cache diagnostic data store") },
    { FLDATAD (HLTENAB, ka_hltenab,  0, "KA655 Autoboot/Halt Enable") },
    { NULL }
    };

#define DBG_REGR 0x0001 /* Interval TMR register read access */
#define DBG_REGW 0x0002 /* Interval TMR register write access */
#define DBG_INT  0x0004 /* Interval TMR Interrupt */
#define DBG_SCHD 0x0008 /* Interval TMR Scheduling */
#define DBG_TODR 0x0010 /* TODR register access  */
#define DBG_CNF  0x0020 /* CNF register access  */

DEBTAB sysd_debug[] = {
  {"REGR", DBG_REGR,  "Interval TMR register read access"},
  {"REGW", DBG_REGW,  "Interval TMR register write access"},
  {"INT",  DBG_INT,   "Interval TMR Interrupt"},
  {"SCHD", DBG_SCHD,  "Interval TMR Scheduling"},
  {"TODR", DBG_TODR,  "TODR register access"},
  {"CNF",  DBG_CNF,   "CNF register access"},
  {0}
};

DEVICE sysd_dev = {
    "SYSD", sysd_unit, sysd_reg, NULL,
    2, 16, 16, 1, 16, 8,
    NULL, NULL, &sysd_reset,
    NULL, NULL, NULL,
    &sysd_dib, DEV_DEBUG, 0, sysd_debug, NULL, NULL, &sysd_help, NULL, NULL, 
    &sysd_description
    };

/* ROM: read only memory - stored in a buffered file
   Register space access routines see ROM twice

   ROM access has been 'regulated' to about 1Mhz to avoid issues
   with testing the interval timers in self-test.  Specifically,
   the VAX boot ROM (ka655.bin) contains code which presumes that
   the VAX runs at a particular slower speed when code is running
   from ROM (which is not cached).  These assumptions are built
   into instruction based timing loops. As the host platform gets
   much faster than the original VAX, the assumptions embedded in
   these code loops are no longer valid.
   
   Code has been added to the ROM implementation to limit CPU speed
   to about 500K instructions per second.  This heads off any future
   issues with the embedded timing loops.  
*/

int32 rom_rd (int32 pa)
{
int32 rg = ((pa - ROMBASE) & ROMAMASK) >> 2;
int32 val = rom[rg];

if (rom_unit.flags & UNIT_NODELAY)
    return val;

return sim_rom_read_with_delay (val);
}

void rom_wr_B (int32 pa, int32 val)
{
int32 rg = ((pa - ROMBASE) & ROMAMASK) >> 2;
int32 sc = (pa & 3) << 3;

rom[rg] = ((val & 0xFF) << sc) | (rom[rg] & ~(0xFF << sc));
}

/* ROM examine */

t_stat rom_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if ((vptr == NULL) || (addr & 03))
    return SCPE_ARG;
if (addr >= ROMSIZE)
    return SCPE_NXM;
*vptr = rom[addr >> 2];
return SCPE_OK;
}

/* ROM deposit */

t_stat rom_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if (addr & 03)
    return SCPE_ARG;
if (addr >= ROMSIZE)
    return SCPE_NXM;
rom[addr >> 2] = (uint32) val;
return SCPE_OK;
}

/* ROM reset */

t_stat rom_reset (DEVICE *dptr)
{
if (rom == NULL)
    rom = (uint32 *) calloc (ROMSIZE >> 2, sizeof (uint32));

if (rom == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Read-only memory (ROM)\n\n");
fprintf (st, "The boot ROM consists of a single unit, simulating the 128KB boot ROM.  It\n");
fprintf (st, "has no registers.  The boot ROM can be loaded with a binary byte stream\n");
fprintf (st, "using the LOAD -r command:\n\n");
fprintf (st, "    LOAD -r KA655X.BIN        load ROM image KA655X.BIN\n\n");
fprintf (st, "When the simulator starts running (via the BOOT command), if the ROM has\n");
fprintf (st, "not yet been loaded, an internal 'buit-in' copy of the KA655X.BIN image\n");
fprintf (st, "will be loaded into the ROM address space and execution will be started.\n\n");
fprintf (st, "ROM accesses a use a calibrated delay that slows ROM-based execution to\n");
fprintf (st, "about 500K instructions per second.  This delay is required to make the\n");
fprintf (st, "power-up self-test routines run correctly on very fast hosts.\n");
fprint_set_help (st, dptr);
return SCPE_OK;
}

const char *rom_description (DEVICE *dptr)
{
return "read-only memory";
}

/* NVR: non-volatile RAM - stored in a buffered file */

int32 nvr_rd (int32 pa)
{
int32 rg = (pa - NVRBASE) >> 2;

return nvr[rg];
}

void nvr_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - NVRBASE) >> 2;

if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    nvr[rg] = ((val & mask) << sc) | (nvr[rg] & ~(mask << sc));
    }
else
    nvr[rg] = val;
}

/* NVR examine */

t_stat nvr_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if ((vptr == NULL) || (addr & 03))
    return SCPE_ARG;
if (addr >= NVRSIZE)
    return SCPE_NXM;
*vptr = nvr[addr >> 2];
return SCPE_OK;
}

/* NVR deposit */

t_stat nvr_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if (addr & 03)
    return SCPE_ARG;
if (addr >= NVRSIZE)
    return SCPE_NXM;
nvr[addr >> 2] = (uint32) val;
return SCPE_OK;
}

/* NVR reset */

t_stat nvr_reset (DEVICE *dptr)
{
if (nvr == NULL) {
    nvr = (uint32 *) calloc (NVRSIZE >> 2, sizeof (uint32));
    nvr_unit.filebuf = nvr;
    ssc_cnf = ssc_cnf | SSCCNF_BLO;
    }
if (nvr == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

/* NVR attach */

t_stat nvr_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);
r = attach_unit (uptr, cptr);
if (r != SCPE_OK)
    uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
else {
    uptr->hwmark = (uint32) uptr->capac;
    ssc_cnf = ssc_cnf & ~SSCCNF_BLO;
    }
return r;
}

/* NVR detach */

t_stat nvr_detach (UNIT *uptr)
{
t_stat r;

r = detach_unit (uptr);
if ((uptr->flags & UNIT_ATT) == 0)
    uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
return r;
}

t_stat nvr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Non-volatile Memory (NVR)\n\n");
fprintf (st, "The NVR consists of a single unit, simulating 1KB of battery-backed up memory\n");
fprintf (st, "in the SSC chip.  When the simulator starts, NVR is cleared to 0, and the SSC\n");
fprintf (st, "battery-low indicator is set.  Normally, NVR is saved and restored like other\n");
fprintf (st, "memory in the system.  Alternately, NVR can be attached to a file.  This\n");
fprintf (st, "allows its contents to be saved and restored independently of other memories,\n");
fprintf (st, "so that NVR state can be preserved across simulator runs.\n\n");
fprintf (st, "Successfully loading an NVR image clears the SSC battery-low indicator.\n\n");
return SCPE_OK;
}

const char *nvr_description (DEVICE *dptr)
{
return "non-volatile memory";
}

/* CSI: console storage input */

int32 csrs_rd (void)
{
return (csi_csr & CSICSR_IMP);
}

int32 csrd_rd (void)
{
csi_csr = csi_csr & ~CSR_DONE;
CLR_INT (CSI);
return (csi_unit.buf & 0377);
}

void csrs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    CLR_INT (CSI);
else if ((csi_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    SET_INT (CSI);
csi_csr = (csi_csr & ~CSICSR_RW) | (data & CSICSR_RW);
}

t_stat csi_reset (DEVICE *dptr)
{
csi_unit.buf = 0;
csi_csr = 0;
CLR_INT (CSI);
return SCPE_OK;
}

const char *csi_description (DEVICE *dptr)
{
return "console storage input";
}

/* CSO: console storage output */

int32 csts_rd (void)
{
return (cso_csr & CSOCSR_IMP);
}

void csts_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    CLR_INT (CSO);
else
    if ((cso_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
        SET_INT (CSO);
cso_csr = (cso_csr & ~CSOCSR_RW) | (data & CSOCSR_RW);
}

void cstd_wr (int32 data)
{
cso_unit.buf = data & 0377;
cso_csr = cso_csr & ~CSR_DONE;
CLR_INT (CSO);
sim_activate (&cso_unit, cso_unit.wait);
}

t_stat cso_svc (UNIT *uptr)
{
cso_csr = cso_csr | CSR_DONE;
if (cso_csr & CSR_IE)
    SET_INT (CSO);
if ((cso_unit.flags & UNIT_ATT) == 0)
    return SCPE_OK;
if (putc (cso_unit.buf, cso_unit.fileref) == EOF) {
    sim_perror ("CSO I/O error");
    clearerr (cso_unit.fileref);
    return SCPE_IOERR;
    }
cso_unit.pos = cso_unit.pos + 1;
return SCPE_OK;
}

t_stat cso_reset (DEVICE *dptr)
{
cso_unit.buf = 0;
cso_csr = CSR_DONE;
CLR_INT (CSO);
sim_cancel (&cso_unit);                                 /* deactivate unit */
return SCPE_OK;
}

const char *cso_description (DEVICE *dptr)
{
return "console storage output";
}

/* SYSD: SSC access mechanisms and devices

   - IPR space read/write routines
   - register space read/write routines
   - SSC local register read/write routines
   - SSC console storage UART
   - SSC timers
   - CMCTL local register read/write routines
*/

/* Read/write IPR register space

   These routines implement the SSC's response to IPR's which are
   sent off the CPU chip for processing.
*/

int32 ReadIPR (int32 rg)
{
int32 val;

switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        val = iccs_rd ();
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

    case MT_CSTD:                                       /* CSTD */
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

    case MT_TXDB:                                       /* TXDB */
        val = 0;
        break;

    case MT_TODR:                                       /* TODR */
        val = todr_rd ();
        sim_debug (DBG_TODR, &sysd_dev, "ReadIPR() = 0x%X\n", val);
        break;

    case MT_CADR:                                       /* CADR */
        val = CADR & 0xFF;
        break;

    case MT_MSER:                                       /* MSER */
        val = MSER & 0xFF;
        break;

    case MT_CONPC:                                      /* console PC */
        val = conpc;
        break;

    case MT_CONPSL:                                     /* console PSL */
        val = conpsl;
        break;

    case MT_SID:                                        /* SID */
        val = CVAX_SID | CVAX_UREV;
        break;

    default:
        ssc_bto = ssc_bto | SSCBTO_BTO;                 /* set BTO */
        val = 0;
        break;
        }

return val;
}

void WriteIPR (int32 rg, int32 val)
{
switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        iccs_wr (val);
        break;

    case MT_TODR:                                       /* TODR */
        sim_debug (DBG_TODR, &sysd_dev, "WriteIPR(val=0x%X)\n", val);
        todr_wr (val);
        break;

    case MT_CSRS:                                       /* CSRS */
        csrs_wr (val);
        break;

    case MT_CSRD:                                       /* CSRD */
        break;

    case MT_CSTS:                                       /* CSTS */
        csts_wr (val);
        break;

    case MT_CSTD:                                       /* CSTD */
        cstd_wr (val);
        break;

    case MT_RXCS:                                       /* RXCS */
        rxcs_wr (val);
        break;

    case MT_RXDB:                                       /* RXDB */
        break;

    case MT_TXCS:                                       /* TXCS */
        txcs_wr (val);
        break;

    case MT_TXDB:                                       /* TXDB */
        txdb_wr (val);
        break;

    case MT_CADR:                                       /* CADR */
        CADR = (val & CADR_RW) | CADR_MBO;
        break;

    case MT_MSER:                                       /* MSER */
        MSER = MSER & MSER_HM;
        break;

    case MT_IORESET:                                    /* IORESET */
        ioreset_wr (val);
        break;

    case MT_SID:
    case MT_CONPC:
    case MT_CONPSL:                                     /* halt reg */
        RSVD_OPND_FAULT(WriteIPR);

    default:
        ssc_bto = ssc_bto | SSCBTO_BTO;                 /* set BTO */
        break;
        }
}

/* Read/write I/O register space

   These routines are the 'catch all' for address space map.  Any
   address that doesn't explicitly belong to memory, I/O, or ROM
   is given to these routines for processing.
*/

struct reglink {                                        /* register linkage */
    uint32      low;                                    /* low addr */
    uint32      high;                                   /* high addr */
    int32       (*read)(int32 pa);                      /* read routine */
    void        (*write)(int32 pa, int32 val, int32 lnt); /* write routine */
    };

struct reglink regtable[] = {
    { CQMAPBASE, CQMAPBASE+CQMAPSIZE, &cqmap_rd, &cqmap_wr },
    { ROMBASE, ROMBASE+ROMSIZE+ROMSIZE, &rom_rd, NULL },
    { NVRBASE, NVRBASE+NVRSIZE, &nvr_rd, &nvr_wr },
    { CMCTLBASE, CMCTLBASE+CMCTLSIZE, &cmctl_rd, &cmctl_wr },
    { SSCBASE, SSCBASE+SSCSIZE, &ssc_rd, &ssc_wr },
    { KABASE, KABASE+KASIZE, &ka_rd, &ka_wr },
    { CQBICBASE, CQBICBASE+CQBICSIZE, &cqbic_rd, &cqbic_wr },
    { CQIPCBASE, CQIPCBASE+CQIPCSIZE, &cqipc_rd, &cqipc_wr },
    { CDGBASE, CDGBASE+CDGSIZE, &cdg_rd, &cdg_wr },
    { 0, 0, NULL, NULL }
    };

/* ReadReg - read register space

   Inputs:
        pa      =       physical address
        lnt     =       length (BWLQ) - ignored
   Output:
        longword of data
*/

int32 ReadReg (uint32 pa, int32 lnt)
{
struct reglink *p;

for (p = &regtable[0]; p->low != 0; p++) {
    if ((pa >= p->low) && (pa < p->high) && p->read)
        return p->read (pa);
    }
ssc_bto = ssc_bto | SSCBTO_BTO | SSCBTO_RWT;
MACH_CHECK (MCHK_READ);
return 0;
}

/* ReadRegU - read register space, unaligned

   Inputs:
        pa      =       physical address
        lnt     =       length in bytes (1, 2, or 3)
   Output:
        returned data, not shifted
*/

int32 ReadRegU (uint32 pa, int32 lnt)
{
return ReadReg (pa & ~03, L_LONG);
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
struct reglink *p;

for (p = &regtable[0]; p->low != 0; p++) {
    if ((pa >= p->low) && (pa < p->high) && p->write) {
        p->write (pa, val, lnt);  
        return;
        }
    }
ssc_bto = ssc_bto | SSCBTO_BTO | SSCBTO_RWT;
MACH_CHECK (MCHK_WRITE);
}

/* WriteRegU - write register space, unaligned

   Inputs:
        pa      =       physical address
        val     =       data to write, right justified in 32b longword
        lnt     =       length (1, 2, or 3)
   Outputs:
        none
*/

void WriteRegU (uint32 pa, int32 val, int32 lnt)
{
int32 sc = (pa & 03) << 3;
int32 dat = ReadReg (pa & ~03, L_LONG);

dat = (dat & ~(insert[lnt] << sc)) | ((val & insert[lnt]) << sc);
WriteReg (pa & ~03, dat, L_LONG);
}

/* CMCTL registers

   CMCTL00 - 15 configure memory banks 00 - 15.  Note that they are
   here merely to entertain the firmware; the actual configuration
   of memory is unaffected by the settings here.

   CMCTL16 - error status register

   CMCTL17 - control/diagnostic status register

   The CMCTL registers are cleared at power up.
*/

int32 cmctl_rd (int32 pa)
{
int32 rg = (pa - CMCTLBASE) >> 2;

switch (rg) {

    default:                                            /* config reg */
        return cmctl_reg[rg] & CMCNF_MASK;

    case 16:                                            /* err status */
        return cmctl_reg[rg];

    case 17:                                            /* csr */
        return cmctl_reg[rg] & CMCSR_MASK;

    case 18:                                            /* KA655X ext mem */
        if (MEMSIZE > MAXMEMSIZE)                       /* more than 128MB? */
            return ((int32) MEMSIZE);
        MACH_CHECK (MCHK_READ);
        }

return 0;
}

void cmctl_wr (int32 pa, int32 val, int32 lnt)
{
int32 i, rg = (pa - CMCTLBASE) >> 2;

if (lnt < L_LONG) {                                     /* LW write only */
    int32 sc = (pa & 3) << 3;                           /* shift data to */
    val = val << sc;                                    /* proper location */
    }
switch (rg) {

    default:                                            /* config reg */
        if (val & CMCNF_SRQ) {                          /* sig request? */
            int32 rg_g = rg & ~3;                       /* group of 4 */
            for (i = rg_g; i < (rg_g + 4); i++) {
                cmctl_reg[i] = cmctl_reg[i] & ~CMCNF_SIG;
                if (ADDR_IS_MEM (i * MEM_BANK))
                    cmctl_reg[i] = cmctl_reg[i] | MEM_SIG;
                }
            }
        cmctl_reg[rg] = (cmctl_reg[rg] & ~CMCNF_RW) | (val & CMCNF_RW);
        break;

    case 16:                                            /* err status */
        cmctl_reg[rg] = cmctl_reg[rg] & ~(val & CMERR_W1C);
        break;

    case 17:                                            /* csr */
        cmctl_reg[rg] = val & CMCSR_MASK;
        break;

    case 18:
        MACH_CHECK (MCHK_WRITE);
        }
}

t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
uint32 memsize = (uint32)(MEMSIZE>>20);
uint32 baseaddr = 0;
struct {
    uint32 capacity;
    const char *option;
    } boards[] = {
        { 16, "MS650-BA"},
        {  0, NULL}};
int32 i;

while (memsize > 1) {
    if (baseaddr >= (64<<20)) {
        fprintf(st, "Memory (@0x%08x): %3d Mbytes (Simulated Extended Memory)\n", baseaddr, memsize);
        break;
        }
    for (i=0; boards[i].capacity > memsize; ++i)
        ;
    fprintf(st, "Memory (@0x%08x): %3d Mbytes (%s)\n", baseaddr, boards[i].capacity, boards[i].option);
    memsize -= boards[i].capacity;
    baseaddr += boards[i].capacity<<20;
    }
return SCPE_OK;
}

/* KA655 registers */

int32 ka_rd (int32 pa)
{
int32 rg = (pa - KABASE) >> 2;

switch (rg) {

    case 0:                                             /* CACR */
        return ka_cacr;

    case 1:                                             /* BDR */
        return ka_bdr;
        }

return 0;
}

void ka_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - KABASE) >> 2;

if ((rg == 0) && ((pa & 3) == 0)) {                     /* lo byte only */
    ka_cacr = (ka_cacr & ~(val & CACR_W1C)) | CACR_FIXED;
    ka_cacr = (ka_cacr & ~CACR_RW) | (val & CACR_RW);
    }
}

int32 sysd_hlt_enb (void)
{
return ka_bdr & BDR_BRKENB;
}

/* Cache diagnostic space */

int32 cdg_rd (int32 pa)
{
int32 t, row = CDG_GETROW (pa);

t = cdg_dat[row];
ka_cacr = ka_cacr & ~CACR_DRO;                          /* clear diag */
ka_cacr = ka_cacr |
    (parity ((t >> 24) & 0xFF, 1) << (CACR_V_DPAR + 3)) |
    (parity ((t >> 16) & 0xFF, 0) << (CACR_V_DPAR + 2)) |
    (parity ((t >> 8) & 0xFF, 1) << (CACR_V_DPAR + 1)) |
    (parity (t & 0xFF, 0) << CACR_V_DPAR);
return t;
}

void cdg_wr (int32 pa, int32 val, int32 lnt)
{
int32 row = CDG_GETROW (pa);

if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    int32 t = cdg_dat[row];
    val = ((val & mask) << sc) | (t & ~(mask << sc));
    }
cdg_dat[row] = val;                                     /* store data */
}

int32 parity (int32 val, int32 odd)
{
for ( ; val != 0; val = val >> 1) {
    if (val & 1)
        odd = odd ^ 1;
    }
return odd;
}

/* SSC registers - byte/word merges done in ssc_wr */

int32 ssc_rd (int32 pa)
{
int32 rg = (pa - SSCBASE) >> 2;
int32 val;

switch (rg) {

    case 0x00:                                          /* base reg */
        return ssc_base;

    case 0x04:                                          /* conf reg */
        sim_debug (DBG_CNF, &sysd_dev, "ssc_rd() = 0x%X", ssc_cnf);
        sim_debug_bits_hdr (DBG_CNF, &sysd_dev, " ", ssc_cnf_bits, ssc_cnf, ssc_cnf, 1);
        return ssc_cnf;

    case 0x08:                                          /* bus timeout */
        return ssc_bto;

    case 0x0C:                                          /* output port */
        return ssc_otp & SSCOTP_MASK;

    case 0x1B:                                          /* TODR */
        val = todr_rd ();
        sim_debug (DBG_TODR, &sysd_dev, "ssc_rd() = 0x%X\n", val);
        return val;

    case 0x1C:                                          /* CSRS */
        return csrs_rd ();

    case 0x1D:                                          /* CSRD */
        return csrd_rd ();

    case 0x1E:                                          /* CSTS */
        return csts_rd ();

    case 0x20:                                          /* RXCS */
        return rxcs_rd ();

    case 0x21:                                          /* RXDB */
        return rxdb_rd ();

    case 0x22:                                          /* TXCS */
        return txcs_rd ();

    case 0x40:                                          /* T0CSR */
        return tmr_csr_rd (0);

    case 0x41:                                          /* T0INT */
        return tmr_tir_rd (0);

    case 0x42:                                          /* T0NI */
        sim_debug (DBG_REGR, &sysd_dev, "tmr_tnir_rd(tmr=%d) - 0x%X\n", 0, tmr_tnir[0]);
        return tmr_tnir[0];

    case 0x43:                                          /* T0VEC */
        sim_debug (DBG_REGR, &sysd_dev, "tmr_tivr_rd(tmr=%d) - 0x%X\n", 0, tmr_tivr[0]);
        return tmr_tivr[0];

    case 0x44:                                          /* T1CSR */
        return tmr_csr_rd (1);

    case 0x45:                                          /* T1INT */
        return tmr_tir_rd (1);

    case 0x46:                                          /* T1NI */
        sim_debug (DBG_REGR, &sysd_dev, "tmr_tnir_rd(tmr=%d) - 0x%X\n", 1, tmr_tnir[1]);
        return tmr_tnir[1];

    case 0x47:                                          /* T1VEC */
        sim_debug (DBG_REGR, &sysd_dev, "tmr_tivr_rd(tmr=%d) - 0x%X\n", 1, tmr_tivr[1]);
        return tmr_tivr[1];

    case 0x4C:                                          /* ADS0M */
        return ssc_adsm[0];

    case 0x4D:                                          /* ADS0K */
        return ssc_adsk[0];

    case 0x50:                                          /* ADS1M */
        return ssc_adsm[1];

    case 0x51:                                          /* ADS1K */
        return ssc_adsk[1];
        }

return 0;
}

void ssc_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - SSCBASE) >> 2;

if (lnt < L_LONG) {                                     /* byte or word? */
    int32 sc = (pa & 3) << 3;                           /* merge */
    int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
    int32 t = ssc_rd (pa);
    val = ((val & mask) << sc) | (t & ~(mask << sc));
    }

switch (rg) {

    case 0x00:                                          /* base reg */
        ssc_base = (val & SSCBASE_RW) | SSCBASE_MBO;
        break;

    case 0x04:                                          /* conf reg */
        sim_debug (DBG_CNF, &sysd_dev, "ssc_wr() = 0x%X", ssc_cnf);
        sim_debug_bits_hdr (DBG_CNF, &sysd_dev, " ", ssc_cnf_bits, ssc_cnf, ssc_cnf, 1);
        ssc_cnf = ssc_cnf & ~(val & SSCCNF_W1C);
        ssc_cnf = (ssc_cnf & ~SSCCNF_RW) | (val & SSCCNF_RW);
        break;

    case 0x08:                                          /* bus timeout */
        ssc_bto = ssc_bto & ~(val & SSCBTO_W1C);
        ssc_bto = (ssc_bto & ~SSCBTO_RW) | (val & SSCBTO_RW);
        break;

    case 0x0C:                                          /* output port */
        ssc_otp = val & SSCOTP_MASK;
        break;

    case 0x1B:                                          /* TODR */
        sim_debug (DBG_TODR, &sysd_dev, "ssc_wr(val=0x%X)\n", val);
        todr_wr (val);
        break;

    case 0x1C:                                          /* CSRS */
        csrs_wr (val);
        break;

    case 0x1E:                                          /* CSTS */
        csts_wr (val);
        break;

    case 0x1F:                                          /* CSTD */
        cstd_wr (val);
        break;

    case 0x20:                                          /* RXCS */
        rxcs_wr (val);
        break;

    case 0x22:                                          /* TXCS */
        txcs_wr (val);
        break;

    case 0x23:                                          /* TXDB */
        txdb_wr (val);
        break;

    case 0x40:                                          /* T0CSR */
        tmr_csr_wr (0, val);
        break;

    case 0x42:                                          /* T0NI */
        tmr_tnir[0] = val;
        sim_debug (DBG_REGW, &sysd_dev, "tmr_tnir_wr(tmr=%d) - 0x%X\n", 0, tmr_tnir[0]);
        break;

    case 0x43:                                          /* T0VEC */
        tmr_tivr[0] = val & TMR_VEC_MASK;
        sim_debug (DBG_REGW, &sysd_dev, "tmr_tivr_wr(tmr=%d) - 0x%X\n", 0, tmr_tivr[0]);
        break;

    case 0x44:                                          /* T1CSR */
        tmr_csr_wr (1, val);
        break;

    case 0x46:                                          /* T1NI */
        tmr_tnir[1] = val;
        sim_debug (DBG_REGW, &sysd_dev, "tmr_tnir_wr(tmr=%d) - 0x%X\n", 1, tmr_tnir[1]);
        break;

    case 0x47:                                          /* T1VEC */
        tmr_tivr[1] = val & TMR_VEC_MASK;
        sim_debug (DBG_REGW, &sysd_dev, "tmr_tivr_wr(tmr=%d) - 0x%X\n", 1, tmr_tivr[1]);
        break;

    case 0x4C:                                          /* ADS0M */
        ssc_adsm[0] = val & SSCADS_MASK;
        break;

    case 0x4D:                                          /* ADS0K */
        ssc_adsk[0] = val & SSCADS_MASK;
        break;

    case 0x50:                                          /* ADS1M */
        ssc_adsm[1] = val & SSCADS_MASK;
        break;

    case 0x51:                                          /* ADS1K */
        ssc_adsk[1] = val & SSCADS_MASK;
        break;
        }
}

/* Programmable timers

   The SSC timers, which increment at 1Mhz, cannot be simulated 
   with ticks due to the overhead that would be required for 1M
   clock events per second.  

   The powerup TOY Test sometimes failed its tolerance test.  This was
   due to varying system load causing varying calibration values to be
   used at different times while referencing the TIR.  While timing long
   intervals, we now synchronize the stepping (and calibration) of the
   system tick with the opportunity to reference the value.  This gives
   precise tolerance measurement values (when interval timers are used
   to measure the system clock), regardless of other load issues on the
   host system which might cause varying values of the system clock's
   calibration factor.

   Various ROM activities, including testing the Interval Timers, presume
   that ROM based code execute instructions at 1 instruction per usec.
   To accommodate this, we not only throttle memory accesses to ROM space,
   but we also use instruction based delays when the interval timers are
   programmed from the ROM for short duration delays.
*/

int32 tmr_tir_rd (int32 tmr)
{
if (tmr_csr[tmr] & TMR_CSR_RUN) {           /* running? then interpolate */
    uint32 usecs_remaining, cur_tir;
    const char *tmr_units = NULL;

    if ((ADDR_IS_ROM(fault_PC)) &&                  /* running from ROM and */
        (tmr_inst[tmr])) {                          /* waiting instructions? */
        usecs_remaining = sim_activate_time (&sysd_dev.units[tmr]) - 1;
        tmr_units = "Instructions";
        }
    else {
        usecs_remaining = (uint32)(0xFFFFFFFFLL & (t_uint64)sim_activate_time_usecs (&sysd_dev.units[tmr]));
        tmr_units = "Microseconds";
        }
    cur_tir = ~usecs_remaining + 1;
    sim_debug (DBG_REGR, &sysd_dev, "tmr_tir_rd(tmr=%d) - 0x%X %s - %u usecs, Interpolated while running\n", tmr, cur_tir, tmr_units, usecs_remaining);
    return cur_tir;
    }

sim_debug (DBG_REGR, &sysd_dev, "tmr_tir_rd(tmr=%d) - 0x%X\n", tmr, tmr_tir[tmr]);

return tmr_tir[tmr];
}

int32 tmr_csr_rd (int32 tmr)
{
sim_debug (DBG_REGR, &sysd_dev, "tmr_csr_rd(tmr=%d) - 0x%X", tmr, tmr_csr[tmr]);
sim_debug_bits_hdr (DBG_REGR, &sysd_dev, " ", tmr_csr_bits, tmr_csr[tmr], tmr_csr[tmr], 1);
return tmr_csr[tmr];
}

void tmr_csr_wr (int32 tmr, int32 val)
{
int32 before_tmr_csr;

if ((tmr < 0) || (tmr > 1))
    return;

before_tmr_csr = tmr_csr[tmr];
sim_debug (DBG_REGW, &sysd_dev, "tmr_csr_wr(tmr=%d) - Writing 0x%X", tmr, val);
sim_debug_bits_hdr (DBG_REGW, &sysd_dev, " ", tmr_csr_bits, val, val, 1);

if ((val & TMR_CSR_RUN) == 0) {                         /* clearing run? */
    if (tmr_csr[tmr] & TMR_CSR_RUN)                     /* run 1 -> 0? */
        tmr_tir[tmr] = tmr_tir_rd (tmr);                /* update itr */
    sim_cancel (&sysd_unit[tmr]);                       /* cancel timer */
    }
tmr_csr[tmr] = tmr_csr[tmr] & ~(val & TMR_CSR_W1C);     /* W1C csr */
tmr_csr[tmr] = (tmr_csr[tmr] & ~TMR_CSR_RW) |           /* new r/w */
    (val & TMR_CSR_RW);
sim_debug (DBG_REGW, &sysd_dev, "tmr_csr_wr(tmr=%d) - ", tmr);
sim_debug_bits_hdr (DBG_REGW, &sysd_dev, "Result", tmr_csr_bits, before_tmr_csr, tmr_csr[tmr], 1);
if (val & TMR_CSR_XFR) {                                /* xfr set? */
    tmr_tir[tmr] = tmr_tnir[tmr];
    sim_debug (DBG_REGW, &sysd_dev, "tmr_csr_wr(tmr=%d) - XFR set TIR=0x%X\n", tmr, tmr_tir[tmr]);
    }
if (val & TMR_CSR_RUN)  {                               /* run? */
    if (val & TMR_CSR_XFR)                              /* new tir? */
        sim_cancel (&sysd_unit[tmr]);                   /* stop prev */
    if (!sim_is_active (&sysd_unit[tmr]))               /* not running? */
        tmr_sched (tmr);                                /* activate */
    }
else
    if (val & TMR_CSR_SGL) {                            /* single step? */
        tmr_incr (tmr, 1);                              /* incr tmr */
        if (tmr_tir[tmr] == 0)                          /* if ovflo, */
            tmr_tir[tmr] = tmr_tnir[tmr];               /* reload tir */
        }
if ((before_tmr_csr & (TMR_CSR_DON | TMR_CSR_IE)) &&
    ((tmr_csr[tmr] & (TMR_CSR_DON | TMR_CSR_IE)) == 0)) {/* update int */
    sim_debug (DBG_INT, &sysd_dev, "tmr_csr_wr(tmr=%d) - CLR_INT\n", tmr);
    if (tmr)
        CLR_INT (TMR1);
    else
        CLR_INT (TMR0);
    }
}

/* Unit service */

t_stat tmr_svc (UNIT *uptr)
{
int32 tmr = uptr - sysd_dev.units;                      /* get timer # */
uint32 delta_usecs = ~tmr_tir[tmr] + 1;

tmr_incr (tmr, delta_usecs);                            /* incr timer */
return SCPE_OK;
}

/* Timer increment */

void tmr_incr (int32 tmr, uint32 inc)
{
uint32 new_tir = tmr_tir[tmr] + inc;                    /* add incr */

if (new_tir < tmr_tir[tmr]) {                           /* ovflo? */
    tmr_tir[tmr] = 0;                                   /* now 0 */
    if (tmr_csr[tmr] & TMR_CSR_DON)                     /* done aready set? */
        tmr_csr[tmr] = tmr_csr[tmr] | TMR_CSR_ERR;      /*  set err */
    else
        tmr_csr[tmr] = tmr_csr[tmr] | TMR_CSR_DON;      /*  set done */
    if (tmr_csr[tmr] & TMR_CSR_STP)                     /* stop? */
        tmr_csr[tmr] = tmr_csr[tmr] & ~TMR_CSR_RUN;     /*  clr run */
    if (tmr_csr[tmr] & TMR_CSR_RUN) {                   /* run? */
        tmr_tir[tmr] = tmr_tnir[tmr];                   /*  reload */
        tmr_sched (tmr);                                /*  reactivate */
        }
    if (tmr_csr[tmr] & TMR_CSR_IE) {                    /* set int req */
        sim_debug (DBG_INT, &sysd_dev, "tmr_incr(tmr=%d) - SET_INT\n", tmr);
        if (tmr)
            SET_INT (TMR1);
        else 
            SET_INT (TMR0);
        }
    }
else {
    tmr_tir[tmr] = new_tir;                             /* no, upd tir */
    if (tmr_csr[tmr] & TMR_CSR_RUN)                     /* still running? */
        tmr_sched (tmr);                                /* reactivate */
    }
}

/* Timer scheduling */

void tmr_sched (int32 tmr)
{
uint32 usecs_sched = tmr_tir[tmr] ? (~tmr_tir[tmr] + 1) : 0xFFFFFFFF;
double usecs_sched_d = tmr_tir[tmr] ? (double)(~tmr_tir[tmr] + 1) : (1.0 + (double)0xFFFFFFFFu);

sim_cancel (&sysd_unit[tmr]);                       /* Make sure not active */
if ((ADDR_IS_ROM(fault_PC)) &&                      /* running from ROM and */
    (usecs_sched < TMR_INC)) {                      /* short delay? */
    tmr_inst[tmr] = TRUE;                           /* wait for instructions */
    sim_activate (&sysd_unit[tmr], usecs_sched);
    sim_debug (DBG_SCHD, &sysd_dev, "tmr_sched(tmr=%d) - after %u instructions - activate after: %.0f usecs\n", tmr, usecs_sched, sim_activate_time_usecs (&sysd_unit[tmr]));
    }
else {
    tmr_inst[tmr] = FALSE;
    sim_activate_after_d (&sysd_unit[tmr], usecs_sched_d);
    sim_debug (DBG_SCHD, &sysd_dev, "tmr_sched(tmr=%d) - after %.0f usecs - activate after: %.0f usecs\n", tmr, usecs_sched_d, sim_activate_time_usecs (&sysd_unit[tmr]));
    }
}

int32 tmr0_inta (void)
{
sim_debug (DBG_INT, &sysd_dev, "tmr0_inta() - Int Ack - Vector=0x%X\n", tmr_tivr[0]);
return tmr_tivr[0];
}

int32 tmr1_inta (void)
{
sim_debug (DBG_INT, &sysd_dev, "tmr1_inta() - Int Ack - Vector=0x%X\n", tmr_tivr[1]);
return tmr_tivr[1];
}

/* Machine check */

int32 machine_check (int32 p1, int32 opc, int32 cc, int32 delta)
{
int32 i, st1, st2, p2, hsir, acc;

if (in_ie)                                              /* in exc? panic */
    ABORT (STOP_INIE);
if (p1 & 0x80)                                          /* mref? set v/p */
    p1 = p1 + mchk_ref;
p2 = mchk_va + 4;                                       /* save vap */
for (i = hsir = 0; i < 16; i++) {                       /* find hsir */
    if ((SISR >> i) & 1)
        hsir = i;
    }
st1 = ((((uint32) opc) & 0xFF) << 24) |
    (hsir << 16) |
    ((CADR & 0xFF) << 8) |
    (MSER & 0xFF);
st2 = 0x00C07000 + (delta & 0xFF);
cc = intexc (SCB_MCHK, cc, 0, IE_SVE);                  /* take exception */
acc = ACC_MASK (KERN);                                  /* in kernel mode */
in_ie = 1;
SP = SP - 20;                                           /* push 5 words */
Write (SP, 16, L_LONG, WA);                             /* # bytes */
Write (SP + 4, p1, L_LONG, WA);                         /* mcheck type */
Write (SP + 8, p2, L_LONG, WA);                         /* address */
Write (SP + 12, st1, L_LONG, WA);                       /* state 1 */
Write (SP + 16, st2, L_LONG, WA);                       /* state 2 */
in_ie = 0;
return cc;
}

/* Console entry */

int32 con_halt (int32 code, int32 cc)
{
int32 temp;

conpc = PC;                                             /* save PC */
conpsl = ((PSL | cc) & 0xFFFF00FF) | CON_HLTINS;        /* PSL, param */
temp = (PSL >> PSL_V_CUR) & 0x7;                        /* get is'cur */
if (temp > 4)                                           /* invalid? */
    conpsl = conpsl | CON_BADPSL;
else STK[temp] = SP;                                    /* save stack */
if (mapen)                                              /* mapping on? */
    conpsl = conpsl | CON_MAPON;
mapen = 0;                                              /* turn off map */
SP = IS;                                                /* set SP from IS */
PSL = PSL_IS | PSL_IPL1F;                               /* PSL = 41F0000 */
JUMP (ROMBASE);                                         /* PC = 20040000 */
return 0;                                               /* new cc = 0 */
}

/* Special boot command - linked into SCP by initial reset

   Syntax: BOOT {CPU}

*/

t_stat vax_boot (int32 flag, CONST char *ptr)
{
char gbuf[CBUFSIZE];

if ((ptr = get_sim_sw (ptr)) == NULL)               /* get switches */
    return SCPE_INVSW;
get_glyph (ptr, gbuf, 0);                           /* get glyph */
if (gbuf[0] && strcmp (gbuf, "CPU"))
    return SCPE_ARG;                                /* Only can specify CPU device */
return run_cmd (flag, "CPU");
}


/* Bootstrap */

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
t_stat r;

PC = ROMBASE;
PSL = PSL_IS | PSL_IPL1F;
conpc = 0;
conpsl = PSL_IS | PSL_IPL1F | CON_PWRUP;
if (rom == NULL)
    return SCPE_IERR;
if (*rom == 0) {                                        /* no boot? */
    r = cpu_load_bootcode (BOOT_CODE_FILENAME, BOOT_CODE_ARRAY, BOOT_CODE_SIZE, TRUE, 0);
    if (r != SCPE_OK)
        return r;
    }
rom_wr_B (ROMBASE+4, sys_model ? 1 : 2);                /* Set Magic Byte to determine system type */
sysd_powerup ();
return SCPE_OK;
}

t_stat sysd_set_halt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
ka_hltenab = val;
if (ka_hltenab)
    ka_bdr |= BDR_BRKENB;
else
    ka_bdr &= ~BDR_BRKENB;
return SCPE_OK;
}

t_stat sysd_show_halt (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf(st, "%s", ka_hltenab ? "NOAUTOBOOT" : "AUTOBOOT");
return SCPE_OK;
}

/* SYSD reset */

t_stat sysd_reset (DEVICE *dptr)
{
int32 i;

if (sim_switches & SWMASK ('P')) sysd_powerup ();       /* powerup? */
for (i = 0; i < 2; i++) {
    tmr_csr[i] = tmr_tnir[i] = tmr_tir[i] = 0;
    tmr_inst[i] = FALSE;
    sim_cancel (&sysd_unit[i]);
    }
csi_csr = 0;
csi_unit.buf = 0;
sim_cancel (&csi_unit);
CLR_INT (CSI);
cso_csr = CSR_DONE;
cso_unit.buf = 0;
sim_cancel (&cso_unit);
CLR_INT (CSO);
sim_vm_cmd = vax_cmd;
return SCPE_OK;
}

/* SYSD powerup */

t_stat sysd_powerup (void)
{
int32 i;

for (i = 0; i < (CMCTLSIZE >> 2); i++)
    cmctl_reg[i] = 0;
for (i = 0; i < 2; i++) {
    tmr_tivr[i] = 0;
    ssc_adsm[i] = ssc_adsk[i] = 0;
    }
ka_cacr = 0;
ssc_base = SSCBASE;
ssc_cnf = ssc_cnf & SSCCNF_BLO;
ssc_bto = 0;
ssc_otp = 0;
return SCPE_OK;
}

t_stat sysd_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "System Devices (SYSD)\n\n");
fprintf (st, "The system devices are the system-specific facilities implemented in the CVAX\n");
fprintf (st, "chip, the KA655 CPU board, the CMCTL memory controller, and the SSC\n");
fprintf (st, "system support chip.  Note that the simulation of these devices is incomplete\n");
fprintf (st, "and is intended strictly to allow the patched bootstrap and console code to\n");
fprintf (st, "run.\n");
fprint_reg_help (st, dptr);
fprintf (st, "\nBDR<7> is the halt-enabled switch.  It controls how the console firmware\n");
fprintf (st, "responds to a BOOT command, a kernel halt (if option CONHALT is set), or a\n");
fprintf (st, "console halt (BREAK typed on the console terminal).  If BDR<7> is set, the\n");
fprintf (st, "onsole firmware responds to all these conditions by entering its interactive\n");
fprintf (st, "command mode.  If BDR<7> is clear, the console firmware boots the operating\n");
fprintf (st, "system in response to these conditions.  This bit can be set and cleared by\n");
fprintf (st, "the command \"SET CPU AUTOBOOT\" (clearing the flag) and \"SET CPU NOAUTOBOOT\"\n");
fprintf (st, "setting the flag.  The default value is set.\n");
return SCPE_OK;
}

const char *sysd_description (DEVICE *dptr)
{
return "system devices";
}

t_stat cpu_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
char gbuf[CBUFSIZE];

if ((cptr == NULL) || (!*cptr))
    return SCPE_ARG;
cptr = get_glyph (cptr, gbuf, 0);
if (MATCH_CMD(gbuf, "VAXSERVER") == 0) {
    sys_model = 0;
    strcpy (sim_name, "VAXserver 3900 (KA655)");
    }
else if (MATCH_CMD(gbuf, "MICROVAX") == 0) {
    sys_model = 1;
    strcpy (sim_name, "MicroVAX 3900 (KA655)");
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
    vc_dev.flags = vc_dev.flags | DEV_DIS;               /* disable QVSS */
    lk_dev.flags = lk_dev.flags | DEV_DIS;               /* disable keyboard */
    vs_dev.flags = vs_dev.flags | DEV_DIS;               /* disable mouse */
    reset_all (0);                                       /* reset everything */
#endif
    }
else if (MATCH_CMD(gbuf, "VAXSTATION") == 0) {
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
    strcpy (sim_name, "VAXstation 3900 (KA655)");
    sys_model = 1;
    vc_dev.flags = vc_dev.flags & ~DEV_DIS;              /* enable QVSS */
    lk_dev.flags = lk_dev.flags & ~DEV_DIS;              /* enable keyboard */
    vs_dev.flags = vs_dev.flags & ~DEV_DIS;              /* enable mouse */
    reset_all (0);                                       /* reset everything */
#else
    return sim_messagef(SCPE_ARG, "Simulator built without Graphic Device Support\n");
#endif
    }
else
    return SCPE_ARG;
return SCPE_OK;
}

t_stat cpu_print_model (FILE *st)
{
fprintf (st, "%s 3900 (KA655)", (sys_model ? "MicroVAX" : "VAXserver"));
return SCPE_OK;
}

t_stat cpu_model_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Notes on memory size:\n\n");
fprintf (st, "- The real KA655 CPU only supported 16MB to 64MB of memory.  The simulator\n");
fprintf (st, "  implements a KA655\"X\", which increases supported memory to 512MB.\n");
fprintf (st, "- The firmware (ka655x.bin) contains code to determine the size of extended\n");
fprintf (st, "  memory and set up the PFN bit map accordingly.  Other than setting up the\n");
fprintf (st, "  PFN bit map, the firmware does not recognize extended memory and will\n");
fprintf (st, "  behave as though memory size was 64MB.\n");
fprintf (st, "- If memory size is being reduced, and the memory being truncated contains\n");
fprintf (st, "  non-zero data, the simulator asks for confirmation.  Data in the truncated\n");
fprintf (st, "  portion of memory is lost.\n");
fprintf (st, "- If the simulator is running VMS, the operating system may have a SYSGEN\n");
fprintf (st, "  parameter set called PHYSICAL PAGES (viewable with\n");
fprintf (st, "  \"MCR SYSGEN SHOW PHYSICALPAGES\").  PHYSICALPAGES limits the maximum\n");
fprintf (st, "  number of physical pages of memory the OS will recognize.  If it is set\n");
fprintf (st, "  to a lower value than the new memory size of the machine, then only the\n");
fprintf (st, "  first PHYSICALPAGES of memory will be recognized, otherwise the actual size\n");
fprintf (st, "  of the extended memory will be realized by VMS upon each boot.  Some users\n");
fprintf (st, "  and/or sites may specify the PHYSICALPAGES parameter in the input file to\n");
fprintf (st, "  AUTOGEN (SYS$SYSTEM:MODPARAMS.DAT).  If PHYSICALPAGES is specified there,\n");
fprintf (st, "  it will have to be adjusted before running AUTOGEN to recognize more memory.\n");
fprintf (st, "  The default value for PHYSICALPAGES is 1048576, which describes 512MB of RAM.\n\n");
fprintf (st, "Initial memory size is 16MB.\n\n");
fprintf (st, "The CPU supports the BOOT command and is the only VAX device to do so.  Note\n");
fprintf (st, "that the behavior of the bootstrap depends on the capabilities of the console\n");
fprintf (st, "terminal emulator.  If the terminal window supports full VT100 emulation\n");
fprintf (st, "(including Multilanguage Character Set support), the bootstrap will ask the\n");
fprintf (st, "user to specify the language; otherwise, it will default to English.\n\n");
fprintf (st, "The simulator is booted with the BOOT command:\n\n");
fprintf (st, "   sim> BOOT\n\n");
return SCPE_OK;
}
