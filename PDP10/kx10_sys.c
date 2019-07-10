/* ka10_sys.c: PDP-10 simulator interface
   Derived from Bob Supnik's pdp10_sys.c


   Copyright (c) 2005-2009, Robert M Supnik
   Copyright (c) 2011-2018, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell.
*/

#include "kx10_defs.h"
#include "sim_card.h"
#include <ctype.h>


/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

#if KLB
char sim_name[] = "KL-10B";
#endif
#if KLA
char sim_name[] = "KL-10A";
#endif
#if KI
char sim_name[] = "KI-10";
#endif
#if KA
char sim_name[] = "KA-10";
#endif
#if PDP6
char sim_name[] = "PDP6";
#endif

extern REG cpu_reg[];
REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;

DEVICE *sim_devices[] = {
    &cpu_dev,
#if PDP6 | KA | KI
    &cty_dev,
#endif
#if (NUM_DEVS_PT > 0)
    &ptp_dev,
    &ptr_dev,
#endif
#if (NUM_DEVS_LP > 0)
    &lpt_dev,
#endif
#if (NUM_DEVS_CR > 0)
    &cr_dev,
#endif
#if (NUM_DEVS_CP > 0)
    &cp_dev,
#endif
#if (NUM_DEVS_DCT > 0)
    &dct_dev,
#endif
#if (NUM_DEVS_MT > 0)
    &mt_dev,
#endif
#if (NUM_DEVS_MTC > 0)
    &mtc_dev,
#endif
#if (NUM_DEVS_DP > 0)
    &dpa_dev,
#if (NUM_DEVS_DP > 1)
    &dpb_dev,
#if (NUM_DEVS_DP > 2)
    &dpc_dev,
#if (NUM_DEVS_DP > 3)
    &dpd_dev,
#endif
#endif
#endif
#endif
#if (NUM_DEVS_RS > 0)
    &rsa_dev,
#endif
#if (NUM_DEVS_RP > 0)
    &rpa_dev,
#if (NUM_DEVS_RP > 1)
    &rpb_dev,
#if (NUM_DEVS_RP > 2)
    &rpc_dev,
#if (NUM_DEVS_RP > 3)
    &rpd_dev,
#endif
#endif
#endif
#endif
#if (NUM_DEVS_TU > 0)
    &tua_dev,
#endif
#if (NUM_DEVS_DSK > 0)
    &dsk_dev,
#endif
#if (NUM_DEVS_RC > 0)
    &rca_dev,
#if (NUM_DEVS_RC > 1)
    &rcb_dev,
#endif
#endif
#if (NUM_DEVS_PMP > 0)
    &pmp_dev,
#endif
#if (NUM_DEVS_DT > 0)
    &dt_dev,
#endif
#if (NUM_DEVS_DTC > 0)
    &dtc_dev,
#endif
#if (NUM_DEVS_DC > 0)
    &dc_dev,
#endif
#if (NUM_DEVS_DCS > 0)
    &dcs_dev,
#endif
#if (NUM_DEVS_DK > 0)
    &dk_dev,
#endif
#if (NUM_DEVS_PD > 0)
    &pd_dev,
#endif
#if (NUM_DEVS_DPY > 0)
    &dpy_dev,
#if (NUM_DEVS_WCNSLS > 0)
    &wcnsls_dev,
#endif
#endif
#if NUM_DEVS_IMP > 0
    &imp_dev,
#endif
#if NUM_DEVS_CH10 > 0
    &ch10_dev,
#endif
#if NUM_DEVS_IMX > 0
    &imx_dev,
#endif
#if NUM_DEVS_STK > 0
#ifdef USE_DISPLAY
    &stk_dev,
#endif
#endif
#if NUM_DEVS_TK10 > 0
    &tk10_dev,
#endif
#if NUM_DEVS_MTY > 0
    &mty_dev,
#endif
#if NUM_DEVS_TEN11 > 0
    &ten11_dev,
#endif
#if NUM_DEVS_AUXCPU > 0
    &auxcpu_dev,
#endif
#if NUM_DEVS_DKB > 0
    &dkb_dev,
#endif
#if NUM_DEVS_DPK > 0
    &dpk_dev,
#endif
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "HALT instruction",
    "Breakpoint"
     };

/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"CONI", DEBUG_CONI, "Show coni instructions"},
    {"CONO", DEBUG_CONO, "Show coni instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
    {0, 0}
};

/* Simulator debug controls */
DEBTAB              crd_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"CONI", DEBUG_CONI, "Show coni instructions"},
    {"CONO", DEBUG_CONO, "Show coni instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
    {"CARD", DEBUG_CARD, "Show Card read/punches"},
    {0, 0}
};

/* Binary loader, supports RIM10, SAV, EXE */

#define FMT_R   1                                       /* RIM10 */
#define FMT_S   2                                       /* SAV */
#define FMT_E   3                                       /* EXE */
#define FMT_D   4                                       /* WAITS DMP */
#define FMT_I   5                                       /* ITS SBLK */

#define EXE_DIR 01776                                   /* EXE directory */
#define EXE_VEC 01775                                   /* EXE entry vec */
#define EXE_PDV 01774                                   /* EXE ignored */
#define EXE_END 01777                                   /* EXE end */

/* WAITS Octal dump loader.

   Simple octal ASCII file, one word per line. All lines which don't
   start with 0-7 are ignored. Everything after the octal constant is
   also ignored.

*/
t_stat load_dmp (FILE *fileref)
{
   char    buffer[100];
   char    *p;
   uint32  addr = 074;
   uint64  data;
   int     high = 0;

   while (fgets((char *)buffer, 80, fileref) != 0) {
        p = (char *)buffer;
        while (*p >= '0' && *p <= '7') {
           data = 0;
           while (*p >= '0' && *p <= '7') {
               data = (data << 3) + *p - '0';
               p++;
           }
           if (addr == 0135 && data != 0)
               high = (uint32)(data & RMASK);
           if (high != 0 && high == addr) {
               addr = 0400000;
               high = 0;
           }
           M[addr++] = data;
           if (*p == ' ' || *p == '\t')
               p++;
        }
   }
   return SCPE_OK;
}


int get_evac(FILE *fileref, uint64 *word)
{
    static uint64 data = 0;
    static int bits = 0;
    unsigned char buf[4];
    unsigned char octet;

    while (bits < 36) {
        if (sim_fread(&octet, 1, 1, fileref) != 1)
            return 1;

        if (octet <= 011 ||
            (octet >= 013 && octet <= 014) ||
            (octet >= 016 && octet <= 0176)) {
            data <<= 7;
            data |= octet;
            bits += 7;
        } else if (octet == 012) {
            data <<= 14;
            data |= 015 << 7 | 012;
            bits += 14;
        } else if (octet == 015) {
            data <<= 7;
            data |= 012;
            bits += 7;
        } else if (octet == 0177) {
            data <<= 14;
            data |= 0177 << 7 | 7;
            bits += 14;
        } else if ((octet >= 0200 && octet <= 0206) ||
                   (octet >= 0210 && octet <= 0211) ||
                   (octet >= 0213 && octet <= 0214) ||
                   (octet >= 0216 && octet <= 0355)) {
            data <<= 14;
            data |= 0177 << 7 | (octet - 0200);
            bits += 14;
        } else if (octet == 0207) {
            data <<= 14;
            data |= 0177 << 7 | 0177;
            bits += 14;
        } else if (octet == 0212) {
            data <<= 14;
            data |= 0177 << 7 | 015;
            bits += 14;
        } else if (octet == 0215) {
            data <<= 14;
            data |= 0177 << 7 | 012;
            bits += 14;
        } else if (octet == 0356) {
            data <<= 7;
            data |= 015;
            bits += 7;
        } else if (octet == 0357) {
            data <<= 7;
            data |= 0177;
            bits += 7;
        } else if (octet >= 0360) {
            if (bits != 0)
                return 1;

            if (sim_fread(&buf, 1, 4, fileref) != 4)
                return 1;

            data = (uint64)(octet & 017) << 32;
            data |= (uint64)(buf[0]) << 24;
            data |= (uint64)(buf[1]) << 16;
            data |= (uint64)(buf[2]) << 8;
            data |= (uint64)(buf[3]);
            bits = 36;
        }

        if (bits == 35) {
            data <<= 1;
            bits++;
        }
    }

    if (bits == 42) {
        *word = (data >> 6) & -2LL;
        data &= 0177;
        bits = 7;
    } else {
        *word = data;
        data = 0;
        bits = 0;
    }

    return 0;
}

/* ITS SBLK loader.

   The SBLK format is similar to SAV.  However, ITS files stored as
   octet streams are usually in the "evacuate" format.
*/
t_stat load_sblk (FILE *fileref)
{
    uint64 word;
    uint64 check;
    int count, addr;

    /* First, skip over the paper tape bootstrap.  It ends with a
       JRST 1. */
    do {
        if (get_evac (fileref, &word))
            return SCPE_FMT;
    } while (word != JRST1);

    /* Load all "simple blocks".  They start with an AOBJN pointer,
       and then comes the data.  Last is a checksum word.  */
    while (get_evac (fileref, &word) == 0 && (word & SMASK)) {
        check = word;
        count = (int)((((word >> 18) ^ RMASK) + 1) & RMASK);
        addr = word & RMASK;
        while (count-- > 0) {
            if (get_evac (fileref, &word))
                return SCPE_FMT;
            M[addr++] = word;
            check = (check << 1) + (check >> 35) + word;
            check &= FMASK;
        }
        if (get_evac (fileref, &word))
            return SCPE_FMT;
        if (check != word)
            return SCPE_FMT;
    }

    /* After the simple blocks comes a start instruction.  It should
       be a JRST or JUMPA. */
    if ((word >> 27) != OP_JRST && (word >> 27) != OP_JUMPA)
        return SCPE_FMT;

    PC = word & RMASK;

    return SCPE_OK;
}


/* RIM10 loader

   RIM10 format is a binary paper tape format (all data frames
   are 200 or greater).  It consists of blocks containing

        -count,,origin-1
        word
        :
        word
        checksum (includes IOWD)
        :
        JRST start
*/

#define RIM_EOF 0xFFFFFFFFFFFFFFFFLL
uint64 getrimw (FILE *fileref)
{
int32 i, tmp;
uint64 word;

word = 0;
for (i = 0; i < 6;) {
    if ((tmp = getc (fileref)) == EOF)
        return RIM_EOF;
    if (tmp & 0200) {
        word = (word << 6) | ((uint64) tmp & 077);
        i++;
        }
    }
return word;
}
#define TSTS(x) SMASK & (x)
#define AOB(x) FMASK & ((x) + 01000001LL)
t_stat load_rim (FILE *fileref)
{
uint64        count, cksm, data;
t_bool        its_rim;
uint32        pa;
int32         op, i, ldrc;

data = getrimw (fileref);                               /* get first word */
if ((data & AMASK) != 0)                                /* error? SA != 0? */
    return SCPE_FMT;
ldrc = 1 + (RMASK ^ ((int32) ((data >> 18) & RMASK)));  /* get loader count */
if (ldrc == 016)                                        /* 16? RIM10B */
    its_rim = FALSE;
else if (ldrc == 017)                                   /* 17? ITS RIM */
    its_rim = TRUE;
else return SCPE_FMT;                                   /* unknown */

for (i = 0; i < ldrc; i++) {                            /* skip the loader */
    data = getrimw (fileref);
    if (data == RIM_EOF)
        return SCPE_FMT;
    }

for ( ;; ) {                                            /* loop until JRST */
    count = cksm = getrimw (fileref);                   /* get header */
    if (count == RIM_EOF)                               /* read err? */
        return SCPE_FMT;
    if (TSTS (count)) {                                 /* hdr = IOWD? */
        for ( ; TSTS (count); count = AOB (count)) {
            data = getrimw (fileref);                   /* get data wd */
            if (data == RIM_EOF)
                return SCPE_FMT;
            if (its_rim) {                              /* ITS RIM? */
                cksm = (((cksm << 1) | (cksm >> 35))) & FMASK;
                                                        /* add to rotated cksm */
                pa = ((uint32) count) & RMASK;          /* store */
                }
            else {                                      /* RIM10B */
                pa = ((uint32) count + 1) & RMASK;      /* store */
                }
            cksm = (cksm + data) & FMASK;               /* add to cksm */
            M[pa] = data;
            }                                           /* end for */
        data = getrimw (fileref);                       /* get cksm */
        if (data == RIM_EOF)
            return SCPE_FMT;
        if (cksm != data)                               /* test cksm */
            return SCPE_CSUM;
        }                                               /* end if count */
    else {
        op = GET_OP (count);                            /* not IOWD */
        if (op != OP_JRST)                              /* JRST? */
            return SCPE_FMT;
        PC = (uint32) count & RMASK;                    /* set PC */
        break;
        }                                               /* end else */
    }                                                   /* end for */
return SCPE_OK;
}


int get_word(FILE *fileref, uint64 *word)
{
   char cbuf[5];

   if (sim_fread(cbuf, 1, 5, fileref) != 5)
       return 1;
   *word = ((uint64)(cbuf[0]) << 29) |
           ((uint64)(cbuf[1]) << 22) |
           ((uint64)(cbuf[2]) << 15) |
           ((uint64)(cbuf[3]) << 8) |
           ((uint64)(cbuf[4] & 0177) << 1) |
           ((uint64)(cbuf[4] & 0200) >> 7);
    return 0;
}

/* SAV file loader

   SAV format is a disk file format (36b words).  It consists of
   blocks containing:

        -count,,origin-1
        word
        :
        word
        :
        JRST start
*/

t_stat load_sav (FILE *fileref)
{
    uint64 data;
    uint32 pa;
    int32 wc;

    for ( ;; ) {                                        /* loop */
        if (get_word(fileref, &data))
            return SCPE_OK;
        wc = (int32)(data >> 18);
        pa = (uint32) (data & RMASK);
        if (wc == (OP_JRST << 9)) {
            printf("Start addr=%06o\n", pa);
            PC = pa;
            return SCPE_OK;
        }
        while (wc != 0) {
            pa++;
            pa &= RMASK;
            wc++;
            wc &= RMASK;
            if (get_word(fileref, &data))
               return SCPE_FMT;
            M[pa] = data;
        }                                              /* end if  count*/
    }
    return SCPE_OK;
}

/* EXE file loader

   EXE format is a disk file format (36b words).  It consists of
   blocks containing:

        block type,,total words = n
        n - 1 data words

   Block types are

        EXE_DIR (1776)  directory
        EXE_VEC (1775)  entry vector
        EXE_PDV (1774)  optional blocks
        EXE_END (1777)  end block

   The directory blocks are the most important and contain doubleword
   page loading information:

        word0<0:8>      =       flags
            <9:35>      =       page in file (0 if 0 page)
        word1<0:8>      =       repeat count - 1
            <9:35>      =       page in memory
*/
#define PAG_SIZE 01000
#define PAG_V_PN 9
#define DIRSIZ  (2 * PAG_SIZE)

t_stat load_exe (FILE *fileref)
{
uint64 data, dirbuf[DIRSIZ], pagbuf[PAG_SIZE], entbuf[2];
int32 ndir, entvec, i, j, k, cont, bsz, bty, rpt, wc;
int32 fpage, mpage;
uint32 ma;

ndir = entvec = 0;                                      /* no dir, entvec */
cont = 1;
do {
    wc = sim_fread (&data, sizeof (uint64), 1, fileref);/* read blk hdr */
    if (wc == 0)                                        /* error? */
        return SCPE_FMT;
    bsz = (int32) ((data & RMASK) - 1);                 /* get count */
    if (bsz < 0)                                        /* zero? */
        return SCPE_FMT;
    bty = (int32) LRZ (data);                           /* get type */
    switch (bty) {                                      /* case type */

    case EXE_DIR:                                       /* directory */
        if (ndir != 0)                                  /* got one */
            return SCPE_FMT;
        ndir = sim_fread (dirbuf, sizeof (uint64), bsz, fileref);
        if (ndir < bsz)                                 /* error */
            return SCPE_FMT;
        break;

    case EXE_PDV:                                       /* optional */
        (void)sim_fseek (fileref, bsz * sizeof (uint64), SEEK_CUR);
        break;

    case EXE_VEC:                                       /* entry vec */
        if (bsz != 2)                                   /* must be 2 wds */
            return SCPE_FMT;
        entvec = sim_fread (entbuf, sizeof (uint64), bsz, fileref);
        if (entvec < 2)                                 /* error? */
            return SCPE_FMT;
        cont = 0;                                       /* stop */
        break;

    case EXE_END:                                       /* end */
        if (bsz != 0)                                   /* must be hdr */
            return SCPE_FMT;
        cont = 0;                                       /* stop */
        break;

    default:
        return SCPE_FMT;
        }                                               /* end switch */
    } while (cont);                                     /* end do */

for (i = 0; i < ndir; i = i + 2) {                      /* loop thru dir */
    fpage = (int32) (dirbuf[i] & RMASK);                /* file page */
    mpage = (int32) (dirbuf[i + 1] & RMASK);            /* memory page */
    rpt = ((int32) ((dirbuf[i + 1] >> 27) + 1)) & 0777; /* repeat count */
    for (j = 0; j < rpt; j++, mpage++) {                /* loop thru rpts */
        if (fpage) {                                    /* file pages? */
            (void)sim_fseek (fileref, (fpage << PAG_V_PN) * sizeof (uint64), SEEK_SET);
            wc = sim_fread (pagbuf, sizeof (uint64), PAG_SIZE, fileref);
            if (wc < PAG_SIZE)
                return SCPE_FMT;
            fpage++;
            }
        ma = mpage << PAG_V_PN;                         /* mem addr */
        for (k = 0; k < PAG_SIZE; k++, ma++) {          /* copy buf to mem */
            if (ma > MEMSIZE)
                return SCPE_NXM;
            M[ma] = fpage? (pagbuf[k] & FMASK): 0;
            }                                           /* end copy */
        }                                               /* end rpt */
    }                                                   /* end directory */
if (entvec && entbuf[1])
    PC = (int32) (entbuf[1] & RMASK);               /* start addr */
return SCPE_OK;
}

/* Master loader */

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
uint64 data;
int32 wc, fmt;
extern int32 sim_switches;

fmt = 0;                                                /* no fmt */
if (sim_switches & SWMASK ('R'))                        /* -r? */
    fmt = FMT_R;
else if (sim_switches & SWMASK ('S'))                   /* -s? */
    fmt = FMT_S;
else if (sim_switches & SWMASK ('E'))                   /* -e? */
    fmt = FMT_E;
else if (sim_switches & SWMASK ('D'))                   /* -d? */
    fmt = FMT_D;
else if (sim_switches & SWMASK ('I'))                   /* -i? */
    fmt = FMT_I;
else if (match_ext (fnam, "RIM"))                       /* .RIM? */
    fmt = FMT_R;
else if (match_ext (fnam, "SAV"))                       /* .SAV? */
    fmt = FMT_S;
else if (match_ext (fnam, "EXE"))                       /* .EXE? */
    fmt = FMT_E;
else if (match_ext (fnam, "DMP"))                       /* .DMP? */
    fmt = FMT_D;
else if (match_ext (fnam, "BIN"))                       /* .BIN? */
    fmt = FMT_I;
else {
    wc = sim_fread (&data, sizeof (uint64), 1, fileref);/* read hdr */
    if (wc == 0)                                        /* error? */
        return SCPE_FMT;
    if (LRZ (data) == EXE_DIR)                          /* EXE magic? */
        fmt = FMT_E;
    else if (TSTS (data))                               /* SAV magic? */
        fmt = FMT_S;
    fseek (fileref, 0, SEEK_SET);                       /* rewind */
    }

switch (fmt) {                                          /* case fmt */

    case FMT_R:                                         /* RIM */
        return load_rim (fileref);

    case FMT_S:                                         /* SAV */
        return load_sav (fileref);

    case FMT_E:                                         /* EXE */
        return load_exe (fileref);

    case FMT_D:                                         /* DMP */
        return load_dmp (fileref);

    case FMT_I:                                         /* SBLK */
        return load_sblk (fileref);
        }

printf ("Can't determine load file format\n");
return SCPE_FMT;
}

/* Symbol tables */

#define I_V_FL          39                              /* inst class */
#define I_M_FL          03                              /* class mask */
#define I_AC            000000000000000                 /* AC, address */
#define I_OP            010000000000000                 /* address only */
#define I_IO            020000000000000                 /* classic I/O */
#define I_V_AC          00
#define I_V_OP          01
#define I_V_IO          02

static const uint64 masks[] = {
 0777000000000, 0777740000000,
 0700340000000, 0777777777777
 };

static const char *opcode[] = {
"PORTAL", "JRSTF", "HALT",                              /* AC defines op */
"XJRSTF", "XJEN", "XPCW",
"JEN", "SFM", "XJRST", "IBP",
"JFOV", "JCRY1", "JCRY0", "JCRY", "JOV",


"LUUO00", "LUUO01", "LUUO02", "LUUO03", "LUUO04", "LUUO05", "LUUO06", "LUUO07",
"LUUO10", "LUUO11", "LUUO12", "LUUO13", "LUUO14", "LUUO15", "LUUO16", "LUUO17",
"LUUO20", "LUUO21", "LUUO22", "LUUO23", "LUUO24", "LUUO25", "LUUO26", "LUUO27",
"LUUO30", "LUUO31", "LUUO32", "LUUO33", "LUUO34", "LUUO35", "LUUO36", "LUUO37",
"MUUO40", "MUUO41", "MUUO42", "MUUO43", "MUUO44", "MUUO45", "MUUO46", "MUUO47",
"MUUO50", "MUUO51", "MUUO52", "MUUO53", "MUUO54", "MUUO55", "MUUO56", "MUUO57",
"MUUO60", "MUUO61", "MUUO62", "MUUO63", "MUUO64", "MUUO65", "MUUO66", "MUUO67",
"MUUO70", "MUUO71", "MUUO72", "MUUO73", "MUUO74", "MUUO75", "MUUO76", "MUUO77",

"UJEN",   "MUUO101", "MUUO102", "JSYS", "MUUO104", "MUUO105", "MUUO106",
"DFAD", "DFSB", "DFMP", "DFDV", "DADD", "DSUB", "DMUL", "DDIV",
"DMOVE", "DMOVN", "FIX", "EXTEND", "DMOVEM", "DMOVNM", "FIXR", "FLTR",
"UFA", "DFN", "FSC", "ADJBP", "ILDB", "LDB", "IDPB", "DPB",
"FAD", "FADL", "FADM", "FADB", "FADR", "FADRL", "FADRM", "FADRB",
"FSB", "FSBL", "FSBM", "FSBB", "FSBR", "FSBRL", "FSBRM", "FSBRB",
"FMP", "FMPL", "FMPM", "FMPB", "FMPR", "FMPRL", "FMPRM", "FMPRB",
"FDV", "FDVL", "FDVM", "FDVB", "FDVR", "FDVRL", "FDVRM", "FDVRB",

"MOVE", "MOVEI", "MOVEM", "MOVES", "MOVS", "MOVSI", "MOVSM", "MOVSS",
"MOVN", "MOVNI", "MOVNM", "MOVNS", "MOVM", "MOVMI", "MOVMM", "MOVMS",
"IMUL", "IMULI", "IMULM", "IMULB", "MUL", "MULI", "MULM", "MULB",
"IDIV", "IDIVI", "IDIVM", "IDIVB", "DIV", "DIVI", "DIVM", "DIVB",
"ASH", "ROT", "LSH", "JFFO", "ASHC", "ROTC", "LSHC",
"EXCH", "BLT", "AOBJP", "AOBJN", "JRST", "JFCL", "XCT", "MAP",
"PUSHJ", "PUSH", "POP", "POPJ", "JSR", "JSP", "JSA", "JRA",
"ADD", "ADDI", "ADDM", "ADDB", "SUB", "SUBI", "SUBM", "SUBB",

"CAI", "CAIL", "CAIE", "CAILE", "CAIA", "CAIGE", "CAIN", "CAIG",
"CAM", "CAML", "CAME", "CAMLE", "CAMA", "CAMGE", "CAMN", "CAMG",
"JUMP", "JUMPL", "JUMPE", "JUMPLE", "JUMPA", "JUMPGE", "JUMPN", "JUMPG",
"SKIP", "SKIPL", "SKIPE", "SKIPLE", "SKIPA", "SKIPGE", "SKIPN", "SKIPG",
"AOJ", "AOJL", "AOJE", "AOJLE", "AOJA", "AOJGE", "AOJN", "AOJG",
"AOS", "AOSL", "AOSE", "AOSLE", "AOSA", "AOSGE", "AOSN", "AOSG",
"SOJ", "SOJL", "SOJE", "SOJLE", "SOJA", "SOJGE", "SOJN", "SOJG",
"SOS", "SOSL", "SOSE", "SOSLE", "SOSA", "SOSGE", "SOSN", "SOSG",

"SETZ", "SETZI", "SETZM", "SETZB", "AND", "ANDI", "ANDM", "ANDB",
"ANDCA", "ANDCAI", "ANDCAM", "ANDCAB", "SETM", "SETMI", "SETMM", "SETMB",
"ANDCM", "ANDCMI", "ANDCMM", "ANDCMB", "SETA", "SETAI", "SETAM", "SETAB",
"XOR", "XORI", "XORM", "XORB", "IOR", "IORI", "IORM", "IORB",
"ANDCB", "ANDCBI", "ANDCBM", "ANDCBB", "EQV", "EQVI", "EQVM", "EQVB",
"SETCA", "SETCAI", "SETCAM", "SETCAB", "ORCA", "ORCAI", "ORCAM", "ORCAB",
"SETCM", "SETCMI", "SETCMM", "SETCMB", "ORCM", "ORCMI", "ORCMM", "ORCMB",
"ORCB", "ORCBI", "ORCBM", "ORCBB", "SETO", "SETOI", "SETOM", "SETOB",

"HLL", "HLLI", "HLLM", "HLLS", "HRL", "HRLI", "HRLM", "HRLS",
"HLLZ", "HLLZI", "HLLZM", "HLLZS", "HRLZ", "HRLZI", "HRLZM", "HRLZS",
"HLLO", "HLLOI", "HLLOM", "HLLOS", "HRLO", "HRLOI", "HRLOM", "HRLOS",
"HLLE", "HLLEI", "HLLEM", "HLLES", "HRLE", "HRLEI", "HRLEM", "HRLES",
"HRR", "HRRI", "HRRM", "HRRS", "HLR", "HLRI", "HLRM", "HLRS",
"HRRZ", "HRRZI", "HRRZM", "HRRZS", "HLRZ", "HLRZI", "HLRZM", "HLRZS",
"HRRO", "HRROI", "HRROM", "HRROS", "HLRO", "HLROI", "HLROM", "HLROS",
"HRRE", "HRREI", "HRREM", "HRRES", "HLRE", "HLREI", "HLREM", "HLRES",

"TRN", "TLN", "TRNE", "TLNE", "TRNA", "TLNA", "TRNN", "TLNN",
"TDN", "TSN", "TDNE", "TSNE", "TDNA", "TSNA", "TDNN", "TSNN",
"TRZ", "TLZ", "TRZE", "TLZE", "TRZA", "TLZA", "TRZN", "TLZN",
"TDZ", "TSZ", "TDZE", "TSZE", "TDZA", "TSZA", "TDZN", "TSZN",
"TRC", "TLC", "TRCE", "TLCE", "TRCA", "TLCA", "TRCN", "TLCN",
"TDC", "TSC", "TDCE", "TSCE", "TDCA", "TSCA", "TDCN", "TSCN",
"TRO", "TLO", "TROE", "TLOE", "TROA", "TLOA", "TRON", "TLON",
"TDO", "TSO", "TDOE", "TSOE", "TDOA", "TSOA", "TDON", "TSON",


"BLKI", "DATAI", "BLKO", "DATAO",                       /* classic I/O */
"CONO",  "CONI", "CONSZ", "CONSO",

NULL
};

static const t_int64 opc_val[] = {
 0254040000000+I_OP, 0254100000000+I_OP,
 0254200000000+I_OP, 0254240000000+I_OP, 0254300000000+I_OP, 0254340000000+I_OP,
 0254500000000+I_OP, 0254600000000+I_OP, 0254640000000+I_OP, 0133000000000+I_OP,
 0255040000000+I_OP, 0255100000000+I_OP, 0255200000000+I_OP, 0255300000000+I_OP,
 0255400000000+I_OP,


 0000000000000+I_AC, 0001000000000+I_AC, 0002000000000+I_AC, 0003000000000+I_AC,
 0004000000000+I_AC, 0005000000000+I_AC, 0006000000000+I_AC, 0007000000000+I_AC,
 0010000000000+I_AC, 0011000000000+I_AC, 0012000000000+I_AC, 0013000000000+I_AC,
 0014000000000+I_AC, 0015000000000+I_AC, 0016000000000+I_AC, 0017000000000+I_AC,
 0020000000000+I_AC, 0021000000000+I_AC, 0022000000000+I_AC, 0023000000000+I_AC,
 0024000000000+I_AC, 0025000000000+I_AC, 0026000000000+I_AC, 0027000000000+I_AC,
 0030000000000+I_AC, 0031000000000+I_AC, 0032000000000+I_AC, 0033000000000+I_AC,
 0034000000000+I_AC, 0035000000000+I_AC, 0036000000000+I_AC, 0037000000000+I_AC,
 0040000000000+I_AC, 0041000000000+I_AC, 0042000000000+I_AC, 0043000000000+I_AC,
 0044000000000+I_AC, 0045000000000+I_AC, 0046000000000+I_AC, 0047000000000+I_AC,
 0050000000000+I_AC, 0051000000000+I_AC, 0052000000000+I_AC, 0053000000000+I_AC,
 0054000000000+I_AC, 0055000000000+I_AC, 0056000000000+I_AC, 0057000000000+I_AC,
 0060000000000+I_AC, 0061000000000+I_AC, 0062000000000+I_AC, 0063000000000+I_AC,
 0064000000000+I_AC, 0065000000000+I_AC, 0066000000000+I_AC, 0067000000000+I_AC,
 0070000000000+I_AC, 0071000000000+I_AC, 0072000000000+I_AC, 0073000000000+I_AC,
 0074000000000+I_AC, 0075000000000+I_AC, 0076000000000+I_AC, 0077000000000+I_AC,

 0100000000000+I_AC,                     0102000000000+I_AC, 0103000000000+I_AC,
 0104000000000+I_AC, 0105000000000+I_AC, 0106000000000+I_AC, 0107000000000+I_AC,
 0110000000000+I_AC, 0111000000000+I_AC, 0112000000000+I_AC, 0113000000000+I_AC,
 0114000000000+I_AC, 0115000000000+I_AC, 0116000000000+I_AC, 0117000000000+I_AC,
 0120000000000+I_AC, 0121000000000+I_AC, 0122000000000+I_AC, 0123000000000+I_AC,
 0124000000000+I_AC, 0125000000000+I_AC, 0126000000000+I_AC, 0127000000000+I_AC,
 0130000000000+I_AC, 0131000000000+I_AC, 0132000000000+I_AC, 0133000000000+I_AC,
 0134000000000+I_AC, 0135000000000+I_AC, 0136000000000+I_AC, 0137000000000+I_AC,
 0140000000000+I_AC, 0141000000000+I_AC, 0142000000000+I_AC, 0143000000000+I_AC,
 0144000000000+I_AC, 0145000000000+I_AC, 0146000000000+I_AC, 0147000000000+I_AC,
 0150000000000+I_AC, 0151000000000+I_AC, 0152000000000+I_AC, 0153000000000+I_AC,
 0154000000000+I_AC, 0155000000000+I_AC, 0156000000000+I_AC, 0157000000000+I_AC,
 0160000000000+I_AC, 0161000000000+I_AC, 0162000000000+I_AC, 0163000000000+I_AC,
 0164000000000+I_AC, 0165000000000+I_AC, 0166000000000+I_AC, 0167000000000+I_AC,
 0170000000000+I_AC, 0171000000000+I_AC, 0172000000000+I_AC, 0173000000000+I_AC,
 0174000000000+I_AC, 0175000000000+I_AC, 0176000000000+I_AC, 0177000000000+I_AC,

 0200000000000+I_AC, 0201000000000+I_AC, 0202000000000+I_AC, 0203000000000+I_AC,
 0204000000000+I_AC, 0205000000000+I_AC, 0206000000000+I_AC, 0207000000000+I_AC,
 0210000000000+I_AC, 0211000000000+I_AC, 0212000000000+I_AC, 0213000000000+I_AC,
 0214000000000+I_AC, 0215000000000+I_AC, 0216000000000+I_AC, 0217000000000+I_AC,
 0220000000000+I_AC, 0221000000000+I_AC, 0222000000000+I_AC, 0223000000000+I_AC,
 0224000000000+I_AC, 0225000000000+I_AC, 0226000000000+I_AC, 0227000000000+I_AC,
 0230000000000+I_AC, 0231000000000+I_AC, 0232000000000+I_AC, 0233000000000+I_AC,
 0234000000000+I_AC, 0235000000000+I_AC, 0236000000000+I_AC, 0237000000000+I_AC,
 0240000000000+I_AC, 0241000000000+I_AC, 0242000000000+I_AC, 0243000000000+I_AC,
 0244000000000+I_AC, 0245000000000+I_AC, 0246000000000+I_AC,
 0250000000000+I_AC, 0251000000000+I_AC, 0252000000000+I_AC, 0253000000000+I_AC,
 0254000000000+I_AC, 0255000000000+I_AC, 0256000000000+I_AC, 0257000000000+I_AC,
 0260000000000+I_AC, 0261000000000+I_AC, 0262000000000+I_AC, 0263000000000+I_AC,
 0264000000000+I_AC, 0265000000000+I_AC, 0266000000000+I_AC, 0267000000000+I_AC,
 0270000000000+I_AC, 0271000000000+I_AC, 0272000000000+I_AC, 0273000000000+I_AC,
 0274000000000+I_AC, 0275000000000+I_AC, 0276000000000+I_AC, 0277000000000+I_AC,

 0300000000000+I_AC, 0301000000000+I_AC, 0302000000000+I_AC, 0303000000000+I_AC,
 0304000000000+I_AC, 0305000000000+I_AC, 0306000000000+I_AC, 0307000000000+I_AC,
 0310000000000+I_AC, 0311000000000+I_AC, 0312000000000+I_AC, 0313000000000+I_AC,
 0314000000000+I_AC, 0315000000000+I_AC, 0316000000000+I_AC, 0317000000000+I_AC,
 0320000000000+I_AC, 0321000000000+I_AC, 0322000000000+I_AC, 0323000000000+I_AC,
 0324000000000+I_AC, 0325000000000+I_AC, 0326000000000+I_AC, 0327000000000+I_AC,
 0330000000000+I_AC, 0331000000000+I_AC, 0332000000000+I_AC, 0333000000000+I_AC,
 0334000000000+I_AC, 0335000000000+I_AC, 0336000000000+I_AC, 0337000000000+I_AC,
 0340000000000+I_AC, 0341000000000+I_AC, 0342000000000+I_AC, 0343000000000+I_AC,
 0344000000000+I_AC, 0345000000000+I_AC, 0346000000000+I_AC, 0347000000000+I_AC,
 0350000000000+I_AC, 0351000000000+I_AC, 0352000000000+I_AC, 0353000000000+I_AC,
 0354000000000+I_AC, 0355000000000+I_AC, 0356000000000+I_AC, 0357000000000+I_AC,
 0360000000000+I_AC, 0361000000000+I_AC, 0362000000000+I_AC, 0363000000000+I_AC,
 0364000000000+I_AC, 0365000000000+I_AC, 0366000000000+I_AC, 0367000000000+I_AC,
 0370000000000+I_AC, 0371000000000+I_AC, 0372000000000+I_AC, 0373000000000+I_AC,
 0374000000000+I_AC, 0375000000000+I_AC, 0376000000000+I_AC, 0377000000000+I_AC,

 0400000000000+I_AC, 0401000000000+I_AC, 0402000000000+I_AC, 0403000000000+I_AC,
 0404000000000+I_AC, 0405000000000+I_AC, 0406000000000+I_AC, 0407000000000+I_AC,
 0410000000000+I_AC, 0411000000000+I_AC, 0412000000000+I_AC, 0413000000000+I_AC,
 0414000000000+I_AC, 0415000000000+I_AC, 0416000000000+I_AC, 0417000000000+I_AC,
 0420000000000+I_AC, 0421000000000+I_AC, 0422000000000+I_AC, 0423000000000+I_AC,
 0424000000000+I_AC, 0425000000000+I_AC, 0426000000000+I_AC, 0427000000000+I_AC,
 0430000000000+I_AC, 0431000000000+I_AC, 0432000000000+I_AC, 0433000000000+I_AC,
 0434000000000+I_AC, 0435000000000+I_AC, 0436000000000+I_AC, 0437000000000+I_AC,
 0440000000000+I_AC, 0441000000000+I_AC, 0442000000000+I_AC, 0443000000000+I_AC,
 0444000000000+I_AC, 0445000000000+I_AC, 0446000000000+I_AC, 0447000000000+I_AC,
 0450000000000+I_AC, 0451000000000+I_AC, 0452000000000+I_AC, 0453000000000+I_AC,
 0454000000000+I_AC, 0455000000000+I_AC, 0456000000000+I_AC, 0457000000000+I_AC,
 0460000000000+I_AC, 0461000000000+I_AC, 0462000000000+I_AC, 0463000000000+I_AC,
 0464000000000+I_AC, 0465000000000+I_AC, 0466000000000+I_AC, 0467000000000+I_AC,
 0470000000000+I_AC, 0471000000000+I_AC, 0472000000000+I_AC, 0473000000000+I_AC,
 0474000000000+I_AC, 0475000000000+I_AC, 0476000000000+I_AC, 0477000000000+I_AC,

 0500000000000+I_AC, 0501000000000+I_AC, 0502000000000+I_AC, 0503000000000+I_AC,
 0504000000000+I_AC, 0505000000000+I_AC, 0506000000000+I_AC, 0507000000000+I_AC,
 0510000000000+I_AC, 0511000000000+I_AC, 0512000000000+I_AC, 0513000000000+I_AC,
 0514000000000+I_AC, 0515000000000+I_AC, 0516000000000+I_AC, 0517000000000+I_AC,
 0520000000000+I_AC, 0521000000000+I_AC, 0522000000000+I_AC, 0523000000000+I_AC,
 0524000000000+I_AC, 0525000000000+I_AC, 0526000000000+I_AC, 0527000000000+I_AC,
 0530000000000+I_AC, 0531000000000+I_AC, 0532000000000+I_AC, 0533000000000+I_AC,
 0534000000000+I_AC, 0535000000000+I_AC, 0536000000000+I_AC, 0537000000000+I_AC,
 0540000000000+I_AC, 0541000000000+I_AC, 0542000000000+I_AC, 0543000000000+I_AC,
 0544000000000+I_AC, 0545000000000+I_AC, 0546000000000+I_AC, 0547000000000+I_AC,
 0550000000000+I_AC, 0551000000000+I_AC, 0552000000000+I_AC, 0553000000000+I_AC,
 0554000000000+I_AC, 0555000000000+I_AC, 0556000000000+I_AC, 0557000000000+I_AC,
 0560000000000+I_AC, 0561000000000+I_AC, 0562000000000+I_AC, 0563000000000+I_AC,
 0564000000000+I_AC, 0565000000000+I_AC, 0566000000000+I_AC, 0567000000000+I_AC,
 0570000000000+I_AC, 0571000000000+I_AC, 0572000000000+I_AC, 0573000000000+I_AC,
 0574000000000+I_AC, 0575000000000+I_AC, 0576000000000+I_AC, 0577000000000+I_AC,

 0600000000000+I_AC, 0601000000000+I_AC, 0602000000000+I_AC, 0603000000000+I_AC,
 0604000000000+I_AC, 0605000000000+I_AC, 0606000000000+I_AC, 0607000000000+I_AC,
 0610000000000+I_AC, 0611000000000+I_AC, 0612000000000+I_AC, 0613000000000+I_AC,
 0614000000000+I_AC, 0615000000000+I_AC, 0616000000000+I_AC, 0617000000000+I_AC,
 0620000000000+I_AC, 0621000000000+I_AC, 0622000000000+I_AC, 0623000000000+I_AC,
 0624000000000+I_AC, 0625000000000+I_AC, 0626000000000+I_AC, 0627000000000+I_AC,
 0630000000000+I_AC, 0631000000000+I_AC, 0632000000000+I_AC, 0633000000000+I_AC,
 0634000000000+I_AC, 0635000000000+I_AC, 0636000000000+I_AC, 0637000000000+I_AC,
 0640000000000+I_AC, 0641000000000+I_AC, 0642000000000+I_AC, 0643000000000+I_AC,
 0644000000000+I_AC, 0645000000000+I_AC, 0646000000000+I_AC, 0647000000000+I_AC,
 0650000000000+I_AC, 0651000000000+I_AC, 0652000000000+I_AC, 0653000000000+I_AC,
 0654000000000+I_AC, 0655000000000+I_AC, 0656000000000+I_AC, 0657000000000+I_AC,
 0660000000000+I_AC, 0661000000000+I_AC, 0662000000000+I_AC, 0663000000000+I_AC,
 0664000000000+I_AC, 0665000000000+I_AC, 0666000000000+I_AC, 0667000000000+I_AC,
 0670000000000+I_AC, 0671000000000+I_AC, 0672000000000+I_AC, 0673000000000+I_AC,
 0674000000000+I_AC, 0675000000000+I_AC, 0676000000000+I_AC, 0677000000000+I_AC,

 0700000000000+I_IO, 0700040000000+I_IO, 0700100000000+I_IO, 0700140000000+I_IO,
 0700200000000+I_IO, 0700240000000+I_IO, 0700300000000+I_IO, 0700340000000+I_IO,

 -1
 };

#define NUMDEV  6

static const char *devnam[NUMDEV] = {
 "APR", "PI", "PAG", "CCA", "TIM", "MTR"
 };

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to values
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       status code
*/

#define FMTASC(x) ((x) < 040)? "<%03o>": "%c", (x)
#define SIXTOASC(x) ((x) + 040)

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 i, j, c, cflag, ac, xr, y, dev;
uint64 inst;

inst = val[0];
cflag = (uptr == NULL) || (uptr == &cpu_unit[0]);
if (sw & SWMASK ('A')) {                                /* ASCII? */
    if (inst > 0377)
        return SCPE_ARG;
    fprintf (of, FMTASC ((int32) (inst & 0177)));
    return SCPE_OK;
    }
if (sw & SWMASK ('C')) {                                /* character? */
    for (i = 30; i >= 0; i = i - 6) {
        c = (int32) ((inst >> i) & 077);
        fprintf (of, "%c", SIXTOASC (c));
                }
    return SCPE_OK;
    }
if (sw & SWMASK ('P')) {                                /* packed? */
    for (i = 29; i >= 0; i = i - 7) {
        c = (int32) ((inst >> i) & 0177);
        fprintf (of, FMTASC (c));
                }
    return SCPE_OK;
    }
if (!(sw & SWMASK ('M')))
    return SCPE_ARG;

/* Instruction decode */

ac = GET_AC (inst);
xr = GET_XR (inst);
y = GET_ADDR (inst);
dev = GET_DEV (inst);
for (i = 0; opc_val[i] >= 0; i++) {                     /* loop thru ops */
    j = (int32) ((opc_val[i] >> I_V_FL) & I_M_FL);      /* get class */
    if (((opc_val[i] & FMASK) == (inst & masks[j]))) {  /* match? */
        fprintf (of, "%s ", opcode[i]);                 /* opcode */
        switch (j) {                                    /* case on class */

        case I_V_AC:                                    /* AC + address */
            fprintf (of, "%-o,", ac);                   /* print AC, fall thru */
        case I_V_OP:                                    /* address only */
            if (inst & INST_IND)
                fprintf (of, "@");
            if (xr)
                fprintf (of, "%-o(%-o)", y, xr);
            else fprintf (of, "%-o", y);
            break;

        case I_V_IO:                                    /* I/O */
            if (dev < NUMDEV)
                fprintf (of, "%s,", devnam[dev]);
            else fprintf (of, "%-o,", dev<<2);
            if (inst & INST_IND)
                fprintf (of, "@");
            if (xr)
                fprintf (of, "%-o(%-o)", y, xr);
            else fprintf (of, "%-o", y);
            break;
            }                                           /* end case */
        return SCPE_OK;
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_ARG;
}

/* Get operand, including indirect and index

   Inputs:
        *cptr   =       pointer to input string
        *status =       pointer to error status
   Outputs:
        val     =       output value
*/

t_value get_opnd (const char *cptr, t_stat *status)
{
int32 sign = 0;
t_value val, xr = 0, ind = 0;
const char *tptr;

*status = SCPE_ARG;                                     /* assume fail */
if (*cptr == '@') {
    ind = INST_IND;
    cptr++;
    }
if (*cptr == '+')
    cptr++;
else if (*cptr == '-') {
    sign = 1;
    cptr++;
    }
val = strtotv (cptr, &tptr, 8);
if (val > 0777777)
    return 0;
if (sign)
    val = (~val + 1) & 0777777;
cptr = tptr;
if (*cptr == '(') {
    cptr++;
    xr = strtotv (cptr, &tptr, 8);
    if ((cptr == tptr) || (*tptr != ')') ||
        (xr > 017) || (xr == 0))
        return 0;
    cptr = ++tptr;
    }
if (*cptr == 0)
    *status = SCPE_OK;
return (ind | (xr << 18) | val);
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        uptr    =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 cflag, i, j;
t_value ac, dev;
t_stat r;
char gbuf[CBUFSIZE], cbuf[2*CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit[0]);
while (isspace (*cptr)) cptr++;
memset (cbuf, '\0', sizeof(cbuf));
strncpy (cbuf, cptr, sizeof(cbuf)-7);
cptr = cbuf;
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = (t_value) cptr[0];
    return SCPE_OK;
    }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* sixbit string? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    for (i = 0; i < 6; i++) {
        val[0] = (val[0] << 6);
        if (cptr[i]) val[0] = val[0] |
            ((t_value) ((cptr[i] + 040) & 077));
        }
    return SCPE_OK;
    }
if ((sw & SWMASK ('P')) || ((*cptr == '#') && cptr++)) { /* packed string? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    for (i = 0; i < 5; i++)
        val[0] = (val[0] << 7) | ((t_value) cptr[i]);
    val[0] = val[0] << 1;
    return SCPE_OK;
    }

/* Instruction parse */

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL)
    return SCPE_ARG;
val[0] = opc_val[i] & FMASK;                            /* get value */
j = (int32) ((opc_val[i] >> I_V_FL) & I_M_FL);          /* get class */
switch (j) {                                            /* case on class */

    case I_V_AC:                                        /* AC + operand */
        if (strchr (cptr, ',')) {                       /* AC specified? */
            cptr = get_glyph (cptr, gbuf, ',');         /* get glyph */
            if (gbuf[0]) {                              /* can be omitted */
                ac = get_uint (gbuf, 8, 017 - 1, &r);
                if (r != SCPE_OK)
                    return SCPE_ARG;
                val[0] = val[0] | (ac << INST_V_AC);
                }
            }                                           /* fall through */
    case I_V_OP:                                        /* operand */
        cptr = get_glyph (cptr, gbuf, 0);
        val[0] = val[0] | get_opnd (gbuf, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        break;

    case I_V_IO:                                        /* I/O */
        cptr = get_glyph (cptr, gbuf, ',');             /* get glyph */
        for (dev = 0; (dev < NUMDEV) && (strcmp (devnam[dev], gbuf) != 0); dev++);
        if (dev >= NUMDEV) {
            dev = get_uint (gbuf, 8, INST_M_DEV, &r);
            if (r != SCPE_OK)
                return SCPE_ARG;
            }
        val[0] = val[0] | (dev << INST_V_DEV);
        cptr = get_glyph (cptr, gbuf, 0);
        val[0] = val[0] | get_opnd (gbuf, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        break;
        }                                               /* end case */

if (*cptr != 0)                                         /* junk at end? */
    return SCPE_ARG;
return SCPE_OK;
}
