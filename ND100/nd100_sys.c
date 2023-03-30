/*
 * Copyright (c) 2023 Anders Magnusson.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sim_defs.h"

#include "nd100_defs.h"

char sim_name[] = "ND100";

extern REG cpu_reg[];
REG *sim_PC = &cpu_reg[2];
int32 sim_emax = 1;

DEVICE *sim_devices[] = {
        &cpu_dev,
        &mm_dev,
        &tti_dev,
        &tto_dev,
        &floppy_dev,
        &clk_dev,
        NULL
};

const char *sim_stop_messages[SCPE_BASE] = {
        "Unknown error",
        "Unhandled IOX address",
        "Unknown instruction",
        "Checksum error",
        "Simulator breakpoint",
        "Wait at level 0",
};

static int mlp;

static int
gb(FILE *f)
{
        int w;

        if (f == NULL)
                return rdmem(mlp++);
        w = getc(f) & 0377;
        return w;
}

static int
gw(FILE *f)
{
        int c = gb(f);
        return (c << 8) | gb(f);
}


/*
 * Bootable (BPUN) tape format.
 * Disks can use it as well with a max of 64 words data.  In this case 
 * the bytes are stored in the LSB of the words from beginning of disk.
 * 1kw block should be read at address 0 in memory.
 *
 * A bootable tape consists of nine segments, named A-I.
 *
 * A - Any chars not including '!'
 * B - (optional) octal number terminated by CR (LF ignored).
 * C - (optional) octal number terminated by '!'.
 * D - A '!' delimeter
 * E - Block start address (in memory), two byte, MSB first.
 * F - Word count in G section, two byte, MSB first.
 * G - Words as counted in F section.
 * H - Checksum of G section, one word.
 * I - Action code.  If non-zero, start at address in B, otherwise nothing.
 */

t_stat
sim_load(FILE *f, CONST char *buf, CONST char *fnam, t_bool flag)
{
        int B, C, E, F, H, I;
        int w, i, rv;
        uint16 s;

        rv = SCPE_OK;
        if (sim_switches & SWMASK('D')) {       /* read file from disk */
                mlp = 0;
                for (i = 0; i < 1024; i++) {
                        /* images have MSB first */
                        s = (getc(f) & 0377) << 8;
                        s |= getc(f) & 0377;
                        wrmem(i, s);
                }
                f = NULL;
        }

        /* read B/C section */
        for (B = C = 0;(w = gb(f) & 0177) != '!'; ) {
                switch (w) {
                case '\n':
                        continue;
                case '\r':
                        B = C, C = 0;
                        break;
                case '0': case '1': case '2': case '3': 
                case '4': case '5': case '6': case '7': 
                        C = (C << 3) | (w - '0');
                        break;
                default:
                        B = C = 0;
                }
        }
        printf("B address    %06o\n", B);
        printf("C address    %06o\n", C);
        regP = B;
        printf("Load address %06o\n", E = gw(f));
        printf("Word count   %06o\n", F = gw(f));
        for (i = s = 0; i < F; i++) {
                wrmem(E+i, gw(f));
                s += rdmem(E+i);
        }
        printf("Checksum     %06o\n", H = gw(f));
        if (H != s)
                rv = STOP_CKSUM;
        printf("Execute      %06o\n", I = gw(f));
        printf("Words read   %06o\n", i);
        ald = 0300;     /* from tape reader */
        return rv;
}

static char *nd_mem[] = {
        "stz", "sta", "stt", "stx", "std", "ldd", "stf", "ldf",
        "min", "lda", "ldt", "ldx", "add", "sub", "and", "ora",
        "fad", "fsb", "fmu", "fdv", "mpy", "jmp", "cjp", "jpl",
        "skp", "rop", "mis", "sht", "N/A", "iox", "arg", "bop"
};

static char *jptab[] =
        { "jap", "jan", "jaz", "jaf", "jpc", "jnc", "jxz", "jxn" };

static char *argtab[] =
        { "sab", "saa", "sat", "sax", "aab", "aaa", "aat", "aax" };

static char *boptab[] = {
        "bset zro", "bset one", "bset bcm", "bset bac", 
        "bskp zro", "bskp one", "bskp bcm", "bskp bac", 
        "bstc", "bsta", "bldc", "blda", "banc", "band", "borc", "bora", 
};

static char *dactab[] = { "", "d", "p", "b", "l", "a", "t", "x" };

static char *skptab[] = {
        "eql", "geq", "gre", "mgre", "ueq", "lss", "lst", "mlst"
};

static char *tratab[] = {
        "pans", "sts", "opr", "pgs", "pvl", "iic", "pid", "pie",
        "csr", "actl", "ald", "pes", "pcs14", "pea", "err16", "err17"
};

static char *trrtab[] = {
        "panc", "sts", "lmp", "pcr", "err04", "iie", "pid", "pie",
        "cclr", "lcil", "ucil", "err13", "err14", "eccr", "err16", "err17"
};

t_stat
fprint_sym(FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
        int ins, op, off;

        if (!(sw & SWMASK ('M')))
                return SCPE_ARG;

        op = val[0];
        ins = op & ND_MEMMSK;
        off = SEXT8(op);

#define R(x)    ((x) & 0177777)
        fprintf(of, "%06o\t", op);
        if (ins < ND_CJP || ins == ND_JPL) {
                /* MEMORY REFERENCE INSTRUCTIONS */
                fprintf(of, "%s ", nd_mem[ins >> ND_MEMSH]);
                switch ((op >> 8) & 07) {
                case 0:
                        fprintf(of, "0%o", R(off + addr));
                        break;
                case 1:
                        fprintf(of, "B+0%o", R(off));
                        break;
                case 2:
                        fprintf(of, "(0%o)", R(off + addr));
                        break;
                case 3:
                        fprintf(of, "(B+0%o)", R(off));
                        break;
                case 4:
                        fprintf(of, "0%o+X", R(off));
                        break;
                case 5:
                        fprintf(of, "B+0%o+X", R(off));
                        break;
                case 6:
                        fprintf(of, "(0%o)+X", R(off + addr));
                        break;
                case 7:
                        fprintf(of, "(B+0%o)+X", R(off));
                        break;
                }
#undef R
        } else if (ins == ND_CJP) {
                fprintf(of, "%s 0%o", jptab[(op & ND_CJPMSK) >> ND_CJPSH],
                    off + addr);
        } else if (ins == ND_IOX) {
                fprintf(of, "iox 0%04o", op & ND_IOXMSK);
        } else if (ins == ND_ARG) {
                fprintf(of, "%s 0%o", argtab[(op & ND_CJPMSK) >> ND_CJPSH],
                    off & 0177777);
        } else if (ins == ND_SHT) {
                fprintf(of, "s%c%c ", (op & 0600) == 0600 ? 'a' : 'h',
                    (op & 0200) ? 'd' : (op & 0400) ? 'a' : 't');
                if (op & 03000)
                        fprintf(of, "%s ", (op & 01000) == 01000 ? "rot " :
                            (op & 02000) == 02000 ? "zin" : "lin");
                fprintf(of, "%d", op & 040 ? 32 - (op & 037) : (op & 037));
        } else if (ins == ND_BOP) {
                fprintf(of, "%s ", boptab[(op & ND_BOPMSK) >> ND_BOPSH]);
                fprintf(of, "%d d%s", (op & 0170) >> 3, dactab[op & 7]);
        } else if (ins == ND_MIS) {
                if ((op & 0177400) == 0151000)
                        fprintf(of, "wait 0%o", op & 0377);
                else if (op == ND_MIS_SEX)
                        fprintf(of, "sex");
                else if (op == ND_MIS_REX)
                        fprintf(of, "rex");
                else if (op == ND_MIS_IOF)
                        fprintf(of, "iof");
                else if (op == ND_MIS_ION)
                        fprintf(of, "ion");
                else if (op == ND_MIS_POF)
                        fprintf(of, "pof");
                else if (op == ND_MIS_PON)
                        fprintf(of, "pon");
                else if (op == ND_MIS_PIOF)
                        fprintf(of, "piof");
                else if (op == ND_MIS_PION)
                        fprintf(of, "pion");
                else if (op == ND_MIS_IOXT)
                        fprintf(of, "ioxt");
                else if ((op & ND_MIS_TRMSK) == ND_MIS_TRA)
                        fprintf(of, "tra %s", tratab[op & 017]);
                else if ((op & ND_MIS_TRMSK) == ND_MIS_TRR)
                        fprintf(of, "trr %s", trrtab[op & 017]);
                else if ((op & ND_MIS_TRMSK) == ND_MIS_MCL)
                        fprintf(of, "mcl 0%o", op & 077);
                else if ((op & ND_MIS_TRMSK) == ND_MIS_MST)
                        fprintf(of, "mst 0%o", op & 077);
                else if ((op & 0177600) == 0153600)
                        fprintf(of, "irr 0%02o d%s",
                            (op >> 3) & 017, dactab[op & 07]);
                else if ((op & 0177600) == 0153400)
                        fprintf(of, "irw 0%02o d%s",
                            (op >> 3) & 017, dactab[op & 07]);
                else if ((op & ND_MONMSK) == ND_MON)
                        fprintf(of, "mon 0%o", op & 0377);
                else if ((op & ND_MONMSK) == ND_MIS_NLZ)
                        fprintf(of, "nlz 0%o", op & 0377);
                else if ((op & ND_MIS_RBMSK) == ND_MIS_LRB)
                        fprintf(of, "lrb");
                else if ((op & ND_MIS_RBMSK) == ND_MIS_SRB)
                        fprintf(of, "srb");
                else
                        fprintf(of, "MISSING2: 0%06o", op);
        } else if (ins == ND_ROP) {
                switch (op & ND_ROPMSK) {
                case 0146000: fprintf(of, "radd"); break;
                case 0146600: fprintf(of, "rsub"); break;
                case 0144400: fprintf(of, "rand"); break;
                case 0145400: fprintf(of, "rora"); break;
                case 0145000: fprintf(of, "rexo"); break;
                case 0144000: fprintf(of, "swap"); break;
                case 0146100: fprintf(of, "copy"); break;
                case 0146500: fprintf(of, "rinc"); break;
                default:
                        if ((op & 0177770) == 0146400) {
                                fprintf(of, "rinc %s", dactab[op & 07]);
                                op = 0;
                        } else
                                fprintf(of, "%07o", op & ND_ROPMSK);
                        break;
                }
                if (op)
                        fprintf(of, " s%s to d%s", 
                            dactab[(op & 070) >> 3], dactab[op & 07]);
        } else if (ins == ND_SKP) {
                if (op & 0300) {
                        if (op == ND_SKP_BFILL)
                                fprintf(of, "bfill");
                        else if (op == ND_SKP_MOVB)
                                fprintf(of, "movb");
                        else if (op == ND_SKP_MOVBF)
                                fprintf(of, "movbf");
                        else if (op == ND_SKP_IDENT10)
                                fprintf(of, "ident 10");
                        else if (op == ND_SKP_IDENT11)
                                fprintf(of, "ident 11");
                        else if (op == ND_SKP_IDENT12)
                                fprintf(of, "ident 12");
                        else if (op == ND_SKP_IDENT13)
                                fprintf(of, "ident 13");
                        else if (op == ND_SKP_LBYT)
                                fprintf(of, "lbyt");
                        else if (op == ND_SKP_SBYT)
                                fprintf(of, "sbyt");
                        else if ((op & 0177707) == ND_SKP_EXR)
                                fprintf(of, "exr %s", dactab[(op & 070) >> 3]);
                        else if ((op & 0177700) == ND_SKP_RMPY)
                                fprintf(of, "rmpy %s %s",
                                    dactab[(op & 070) >> 3], dactab[op & 07]);
                        else
                                fprintf(of, "MISSING4: 0%06o", op);
                } else
                        fprintf(of, "skp d%s %s s%s", dactab[op & 07],
                            skptab[(op >> 8) & 07], dactab[(op & 070) >> 3]);
        } else
                fprintf(of, "MISSING: 0%06o", op);

        return SCPE_OK;
}

t_stat
parse_sym(CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
        return SCPE_ARG;
}
