/* lgp_sys.c: LGP-30 simulator interface

   Copyright (c) 2004-2008, Robert M Supnik

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

   04-Jan-05    RMS     Modified VM pointer setup
*/

#include "lgp_defs.h"
#include <ctype.h>

t_stat parse_sym_m (char *cptr, t_value *val, int32 sw);
void lgp_init (void);

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern DEVICE tti_dev, tto_dev;
extern DEVICE ptr_dev, ptp_dev;
extern REG cpu_reg[];
extern uint32 M[];
extern uint32 PC;
extern uint32 ts_flag;
extern int32 flex_to_ascii[128], ascii_to_flex[128];

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             maximum number of words for examine/deposit
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "LGP30";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &tti_dev,
    &tto_dev,
    &ptr_dev,
    &ptp_dev,
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "STOP",
    "Breakpoint",
    "Arithmetic overflow"
    };

/* Binary loader - implements a restricted form of subroutine 10.4

   Switches:
         -t, input file is transposed Flex
         -n, no checksums on v commands (10.0 compatible)
        default is ASCII encoded Flex
   Commands (in bits 0-3):
        (blank)         instruction
        +               command (not supported)
        ;               start fill
        /               set modifier
        .               stop and transfer
        ,               hex words
        v               hex fill (checksummed unless -n)
        8               negative instruction
*/

/* Utility routine - read characters until ' (conditional stop) */

t_stat load_getw (FILE *fi, uint32 *wd)
{
int32 flex, c;

*wd = 0;
while ((c = fgetc (fi)) != EOF) {
    if (sim_switches & SWMASK ('T'))
        flex = ((c << 1) | (c >> 5)) & 0x3F;
    else flex = ascii_to_flex[c & 0x7F];
    if ((flex == FLEX_CR) || (flex == FLEX_DEL) ||
        (flex == FLEX_UC) || (flex == FLEX_LC) ||
        (flex == FLEX_BS) || (flex < 0))
        continue;
    if (flex == FLEX_CSTOP)
        return SCPE_OK;
    *wd = (*wd << 4) | ((flex >> 2) & 0xF);
    }
return SCPE_FMT;
}

/* Utility routine - convert ttss decimal address to binary */

t_stat load_geta (uint32 wd, uint32 *ad)
{
uint32 n1, n2, n3, n4, tr, sc;

n1 = (wd >> 12) & 0xF;
n2 = (wd >> 8) & 0xF;
n3 = (wd >> 4) & 0xF;
n4 = wd & 0xF;
if ((n2 > 9) || (n4 > 9))
    return SCPE_ARG;
tr = (n1 * 10) + n2;
sc = (n3 * 10) + n4;
if ((tr >= NTK_30) || (sc >= NSC_30))
    return SCPE_ARG;
*ad = (tr * NSC_30) + sc;
return SCPE_OK;
}

/* Loader proper */

t_stat sim_load (FILE *fi, char *cptr, char *fnam, int flag)
{
uint32 wd, origin, amod, csum, cnt, tr, sc, ad, cmd;

origin = amod = 0;
for (;;) {                                              /* until stopped */
    if (load_getw (fi, &wd))                            /* get ctrl word */
        break;
    cmd = (wd >> 28) & 0xF;                             /* get <0:3> */
    switch (cmd) {                                      /* decode <0:3> */

        case 0x2:                                       /* + command */
            return SCPE_FMT;

        case 0x3:                                       /* ; start fill */
            if (load_geta (wd, &origin))                /* origin = addr */
                return SCPE_FMT;
            break;

        case 0x4:                                       /* / set modifier */
            if (load_geta (wd, &amod))                  /* modifier = addr */
                return SCPE_FMT;
            break;

        case 0x5:                                       /* . transfer */
            if (load_geta (wd, &PC))                    /* PC = addr */
                return SCPE_FMT;
            return SCPE_OK;                             /* done! */

        case 0x6:                                       /* hex words */
            if (load_geta (wd, &cnt))                   /* count = addr */
                return SCPE_FMT;
            if ((cnt == 0) || (cnt > 63))
                return SCPE_FMT;
            while (cnt--) {                             /* fill hex words */
                if (load_getw (fi, &wd))
                    return SCPE_FMT;
                Write (origin, wd);
                origin = (origin + 1) & AMASK;
                }
            break;

        case 0x7:                                       /* hex fill */
            cnt = (wd >> 16) & 0xFFF;                   /* hex count */
            tr = (wd >> 8) & 0xFF;                      /* hex track */
            sc = wd & 0xFF;                             /* hex sector */
            if ((cnt == 0) || (cnt > 0x7FF) ||          /* validate */
                (tr >= NTK_30) || (sc >= NSC_30))
                return SCPE_ARG;
            ad = (tr * NSC_30) + sc;                    /* decimal addr */
            for (csum = 0; cnt; cnt--) {                /* fill words */
                if (load_getw (fi, &wd))
                    return SCPE_FMT;
                Write (ad, wd);
                csum = (csum + wd) & MMASK;
                ad = (ad + 1) & AMASK;
                }
            if (!(sim_switches & SWMASK ('N'))) {       /* unless -n, csum */
                if (load_getw (fi, &wd))
                    return SCPE_FMT;
/*              if ((csum ^wd) & MMASK)
                    return SCPE_CSUM; */
                }
            break;

        case 0x0: case 0x8:                             /* instructions */
            if (load_geta (wd, &ad))                    /* get address */
                return SCPE_FMT;
            if ((wd & 0x00F00000) != 0x00900000)        /* if not x, */
                ad = (ad + amod) & AMASK;               /* modify */
            wd = (wd & (SIGN|I_OP)) + (ad << I_V_EA);   /* instruction */

        default:                                        /* data word */
            Write (origin, wd);
            origin = (origin + 1) & AMASK;
            break;
            }                                           /* end case */
        }                                               /* end for */
return SCPE_OK;
}

/* Symbol tables */

static const char opcode[] = "ZBYRIDNMPEUTHCAS";

static const char hex_decode[] = "0123456789FGJKQW";

void lgp_fprint_addr (FILE *st, DEVICE *dptr, t_addr addr)
{
if ((dptr == sim_devices[0]) &&
    ((sim_switches & SWMASK ('T')) ||
    ((cpu_unit.flags & UNIT_TTSS_D) && !(sim_switches & SWMASK ('N')))))
    fprintf (st, "%02d%02d", addr >> 6, addr & SCMASK_30);
else fprint_val (st, addr, dptr->aradix, dptr->awidth, PV_LEFT);
return;
}

t_addr lgp_parse_addr (DEVICE *dptr, char *cptr, char **tptr)
{
t_addr ad, ea;

if ((dptr == sim_devices[0]) &&
    ((sim_switches & SWMASK ('T')) ||
    ((cpu_unit.flags & UNIT_TTSS_D) && !(sim_switches & SWMASK ('N'))))) {
    ad = (t_addr) strtotv (cptr, (const char **)tptr, 10);
    if (((ad / 100) >= NTK_30) || ((ad % 100) >= NSC_30)) {
        *tptr = cptr;
        return 0;
        }
    ea = ((ad / 100) * NSC_30) | (ad % 100);
    }
else ea = (t_addr) strtotv (cptr, (const char **)tptr, dptr->aradix);
return ea;
}

void lgp_vm_init (void)
{
sim_vm_fprint_addr = &lgp_fprint_addr;
sim_vm_parse_addr = &lgp_parse_addr;
return;
}

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to data
        *uptr   =       pointer to unit 
        sw      =       switches
   Outputs:
        return  =       status code
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 i, c;
uint32 inst, op, ea;

inst = val[0];
if (sw & SWMASK ('A')) {                                /* alphabetic? */
    if ((uptr == NULL) || !(uptr->flags & UNIT_ATT))
        return SCPE_ARG;
    if (uptr->flags & UNIT_FLEX) {                      /* Flex file? */
        c = flex_to_ascii[inst];                        /* get ASCII equiv */
        if (c <= 0)
            return SCPE_ARG;
        }
    else c = inst & 0x7F;                               /* ASCII file */
    fputc (c, of);
    return SCPE_OK;
    }

if (uptr && (uptr != &cpu_unit))                        /* must be CPU */
    return SCPE_ARG;
if ((sw & SWMASK ('M')) &&                              /* symbolic decode? */
    ((inst & ~(SIGN|I_OP|I_EA)) == 0)) {
	op = I_GETOP (inst);
    ea = I_GETEA (inst);
    if (inst & SIGN)
        fputc ('-', of);
    fprintf (of, "%c ", opcode[op]);
    lgp_fprint_addr (of, sim_devices[0], ea);
    return SCPE_OK;
    }

if ((sw & SWMASK ('L')) ||                              /* LGP hex? */
    ((cpu_unit.flags & UNIT_LGPH_D) && !(sw & SWMASK ('H')))) {
    for (i = 0; i < 8; i++) {
        c = (inst >> (4 * (7 - i))) & 0xF;
        fputc (hex_decode[c], of);
        }
    return SCPE_OK;
    }
return SCPE_ARG;
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        *uptr   =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/

t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 i, c;
char *tptr;

while (isspace (*cptr))                                 /* absorb spaces */
    cptr++;
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) {
    if ((uptr == NULL) || !(uptr->flags & UNIT_ATT))
        return SCPE_ARG;
    if (uptr->flags & UNIT_FLEX) {                      /* Flex file? */        
        c = ascii_to_flex[*cptr & 0x7F];                /* get Flex equiv */
        if (c < 0)
            return SCPE_ARG;
        val[0] = ((c >> 1) | (c << 5)) & 0x3F;          /* transpose */
        }
    else val[0] = *cptr & 0x7F;                         /* ASCII file */
    return SCPE_OK;
    }   

if (uptr && (uptr != &cpu_unit))                        /* must be CPU */
    return SCPE_ARG;
if (!parse_sym_m (cptr, val, sw))                       /* symbolic parse? */
    return SCPE_OK;
if ((sw & SWMASK ('L')) ||                              /* LGP hex? */
    ((cpu_unit.flags & UNIT_LGPH_D) && !(sw & SWMASK ('H')))) {
    val[0] = 0;
    while (isspace (*cptr)) cptr++;                     /* absorb spaces */
    for (i = 0; i < 8; i++) {
        c = *cptr++;                                    /* get char */
        if (c == 0)
            return SCPE_OK;
        if (islower (c))
            c = toupper (c);
        if ((tptr = strchr (hex_decode, c)))
            val[0] = (val[0] << 4) | (tptr - hex_decode);
        else return SCPE_ARG;
        }
    if (*cptr == 0)
        return SCPE_OK;
    }
return SCPE_ARG;
}

/* Instruction parse */

t_stat parse_sym_m (char *cptr, t_value *val, int32 sw)
{
uint32 ea, sgn;
char *tptr, gbuf[CBUFSIZE];

if (*cptr == '-') {
    cptr++;
    sgn = SIGN;
    }
else sgn = 0;
cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
if (gbuf[1] != 0)
    return SCPE_ARG;
if ((tptr = strchr (opcode, gbuf[0])))
    val[0] = ((tptr - opcode) << I_V_OP) | sgn;         /* merge opcode */
else return SCPE_ARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get address */
ea = lgp_parse_addr (sim_devices[0], gbuf, &tptr);
if ((tptr == gbuf) || (*tptr != 0) || (ea > AMASK))
    return SCPE_ARG;
val[0] = val[0] | (ea << I_V_EA);                       /* merge address */
if (*cptr != 0)
    return SCPE_2MARG;
return SCPE_OK;
}
