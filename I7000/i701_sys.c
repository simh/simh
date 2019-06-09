/* i701_sys.c: IBM 701 Simulator system interface.

   Copyright (c) 2005-2016, Richard Cornwell

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

*/

#include "i7090_defs.h"
#include "sim_card.h"
#include <ctype.h>

t_stat  parse_sym(CONST char *cptr, t_addr addr, UNIT * uptr, t_value * val, int32 sw);

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char                sim_name[] = "IBM 701";

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 1;

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &chan_dev,
#ifdef NUM_DEVS_CDR
    &cdr_dev,
#endif
#ifdef NUM_DEVS_CDP
    &cdp_dev,
#endif
#ifdef NUM_DEVS_LPR
    &lpr_dev,
#endif
#ifdef MT_CHANNEL_ZERO
    &mtz_dev,
#endif
#ifdef NUM_DEVS_DR
    &drm_dev,
#endif
    NULL
};

#ifdef NUM_DEVS_CDR
DIB   cdp_dib = { CH_TYP_PIO, 1, 02000, 07777, &cdp_cmd, &cdp_ini };
#endif
#ifdef NUM_DEVS_CDP
DIB   cdr_dib = { CH_TYP_PIO, 1, 04000, 07777, &cdr_cmd, NULL };
#endif
#ifdef NUM_DEVS_DR
DIB   drm_dib = { CH_TYP_PIO, 1, 0200, 07774, &drm_cmd, &drm_ini };
#endif
#ifdef NUM_DEVS_LPR
DIB   lpr_dib = { CH_TYP_PIO, 1, 01000, 07777, &lpr_cmd, &lpr_ini };
#endif
#ifdef MT_CHANNEL_ZERO
DIB   mt_dib = { CH_TYP_PIO, NUM_UNITS_MT, 0400, 07770, &mt_cmd, &mt_ini };
#endif


/* Simulator stop codes */
const char         *sim_stop_messages[] = {
    "Unknown error",
    "IO device not ready",
    "HALT instruction",
    "Breakpoint",
    "Unknown Opcode",
    "Nested indirects exceed limit",
    "Nested XEC's exceed limit",
    "I/O Check opcode",
    "Memory management trap during trap",
    "7750 invalid line number",
    "7750 invalid message",
    "7750 No free output buffers",
    "7750 No free input buffers", "Error?", "Error2", 0
};

/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CHANNEL", DEBUG_CHAN, "Debug Channel use"},
    {"TRAP", DEBUG_TRAP, "Show CPU Traps"},
    {"CMD", DEBUG_CMD, "Show device commands"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show detailed device information"},
    {"EXP", DEBUG_EXP, "Show device exceptions"},
    {"SENSE", DEBUG_SNS, "Show sense data on 7909 channel"},
    {0, 0}
};

DEBTAB              crd_debug[] = {
    {"CHAN", DEBUG_CHAN},
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {"CARD", DEBUG_CARD},
    {0, 0}
};

/* Load a card image file into memory.  */

t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    t_uint64            wd;
    t_uint64            mask;
    int                 addr = 0;
    int                 dlen = 0;
    char               *p;
    char                buf[160];

    if (match_ext(fnam, "crd")) {
        int                 firstcard = 1;
        uint16              cbuf[80];
        t_uint64            lbuff[24];
        int                 i;

        while (sim_fread(cbuf, 2, 80, fileref) == 80) {

            /* Bit flip into read buffer */
            for (i = 0; i < 24; i++) {
                int                 bit = 1 << (i / 2);
                int                 b = 36 * (i & 1);
                int                 col;

                mask = 1;
                wd = 0;
                for (col = 35; col >= 0; mask <<= 1) {
                    if (cbuf[col-- + b] & bit)
                        wd |= mask;
                }
                lbuff[i] = wd;
            }
            i = 2;
            if (firstcard) {
                addr = 0;
                dlen = 3 + (int)((lbuff[0] >> 18) & 077777);
                firstcard = 0;
                i = 0;
            } else if (dlen == 0) {
                addr = (int)(lbuff[0] & 077777);
                dlen = (int)(lbuff[0] >> 18) & 077777;
            }
            for (; i < 24 && dlen > 0; i++) {
                M[addr++] = lbuff[i];
                dlen--;
            }
        }
    } else if (match_ext(fnam, "oct")) {
        while (fgets((char *)buf, 80, fileref) != 0) {
             for(p = (char *)buf; *p == ' ' || *p == '\t'; p++);
            /* Grab address */
             for(addr = 0; *p >= '0' && *p <= '7'; p++)
                addr = (addr << 3) + *p - '0';
             while(*p != '\n' && *p != '\0') {
                for(; *p == ' ' || *p == '\t'; p++);
                for(wd = 0; *p >= '0' && *p <= '7'; p++)
                    wd = (wd << 3) + *p - '0';
                if (addr < MAXMEMSIZE)
                    M[addr++] = wd;
             }
        }
    } else if (match_ext(fnam, "txt")) {
        while (fgets((char *)buf, 80, fileref) != 0) {
             for(p = (char *)buf; *p == ' ' || *p == '\t'; p++);
             /* Grab address */
             for(addr = 0; *p >= '0' && *p <= '7'; p++)
                addr = (addr << 3) + *p - '0';
             while(*p == ' ' || *p == '\t') p++;
             if(sim_strncasecmp(p, "BCD", 3) == 0) {
                 p += 3;
                 parse_sym(++p, addr, &cpu_unit, &M[addr], SWMASK('C'));
             } else if (sim_strncasecmp(p, "OCT", 3) == 0) {
                 p += 3;
                 for(; *p == ' ' || *p == '\t'; p++);
                 parse_sym(p, addr, &cpu_unit, &M[addr], 0);
             } else {
                 parse_sym(p, addr, &cpu_unit, &M[addr], SWMASK('M'));
             }
        }
    } else
        return SCPE_ARG;
    return SCPE_OK;
}

/* Symbol tables */
typedef struct _opcode
{
    uint16              opbase;
    CONST char          *name;
}
t_opcode;

/* Opcodes */
t_opcode            base_ops[] = {
    {0, "STOP"},
    {1, "TR"},
    {2, "TRO"},
    {3, "TRP"},
    {4, "TRZ"},
    {5, "SUB"},
    {6, "R SUB"},
    {7, "SUB AB"},
    {8, "NO OP"},
    {9, "ADD"},
    {10, "R ADD"},
    {11, "ADD AB"},
    {12, "STORE"},
    {13, "STORE A"},
    {14, "STORE MQ"},
    {15, "LOAD MQ"},
    {16, "MPY"},
    {17, "MPY R"},
    {18, "DIV"},
    {19, "ROUND"},
    {20, "L LEFT"},
    {21, "L RIGHT"},
    {22, "A LEFT"},
    {23, "A RIGHT"},
    {24, "READ"},
    {25, "READ B"},
    {26, "WRITE"},
    {27, "WRITE EF"},
    {28, "REWIND"},
    {29, "SET DR"},
    {30, "SENSE"},
    {31, "COPY"},
    {13 + 040, "EXTR"},
    {0, NULL}
};

const char *chname[] = { "*" };



/* Parse address

   Inputs:
        *dptr   =       pointer to device.
        *cptr   =       pointer to string.
        **tptr  =       pointer to final scaned character.
   Outputs:
        address with sign.
*/

t_addr
parse_addr(DEVICE *dptr, const char *cptr, const char **tptr) {
    t_addr      v;
    int         s = 0;
    *tptr = cptr;
    if (dptr != &cpu_dev)
        return 0;
    v = 0;
    if (*cptr == '-') {
        cptr++;
        s = 1;
    }
    while(*cptr >= '0' && *cptr <= '7') {
        v <<= 3;
        v += *cptr++ - '0';
    }
    if (v > 4096)
        return 0;
    if (s) {
        if ((cptr - 1) != *tptr)
            *tptr = cptr;
        v |= 0400000;
    } else {
        if (cptr != *tptr)
            *tptr = cptr;
    }
    return v;
}



void sys_init(void) {
        sim_vm_parse_addr = &parse_addr;
}

void (*sim_vm_init) (void) = &sys_init;

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

t_stat
fprint_sym(FILE * of, t_addr addr, t_value * val, UNIT * uptr, int32 sw)
{
    t_uint64            inst = *val;

/* Print value in octal first */
    fputc(' ', of);
    fprint_val(of, inst, 8, 36, PV_RZRO);

    if (sw & SWMASK('M')) {
        int     op = (int)(inst >> (12+18));
        int     i;
        fputs("  rt  ", of);
        if (op != (040 + 13))
           op &= 037;
        for(i = 0; base_ops[i].name != NULL; i++) {
            if (base_ops[i].opbase == op) {
                fputs(base_ops[i].name, of);
                break;
            }
        }
        fputc(' ', of);
        if ((inst >> 18) & 0400000L)
            fputc('-', of);
        fprint_val(of, (inst >> 18) & 0000000007777L, 8, 12, PV_RZRO);
        op = (int)(inst >> 12);
        fputs(" lt  ", of);
        if (op != (040 + 13))
           op &= 037;
        for(i = 0; base_ops[i].name != NULL; i++) {
            if (base_ops[i].opbase == op) {
                fputs(base_ops[i].name, of);
                break;
            }
        }
        fputc(' ', of);
        if (inst & 0400000L)
            fputc('-', of);
        else
            fputc(' ', of);
        fprint_val(of, inst & 0000000007777L, 8, 12, PV_RZRO);
    }

    if (sw & SWMASK('C')) {
        int                 i;

        fputs("   '", of);
        for (i = 5; i >= 0; i--) {
            int                 ch;

            ch = (int)(inst >> (6 * i)) & 077;
            fputc(sim_six_to_ascii[ch], of);
        }
        fputc('\'', of);
    }
    return SCPE_OK;
}

t_opcode           *
find_opcode(char *op, t_opcode * tab)
{
    while (tab->name != NULL) {
        if (*tab->name != '\0' && strcmp(op, tab->name) == 0)
            return tab;
        tab++;
    }
    return NULL;
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

t_stat
parse_sym(CONST char *cptr, t_addr addr, UNIT * uptr, t_value * val, int32 sw)
{
    int                 i;
    int                 f;
    t_value             d;
    t_addr              tag;
    int                 sign;
    char                opcode[100];
    const char          *arg;

    while (isspace(*cptr))
        cptr++;
    d = 0;
    if (sw & SWMASK('M')) {
        t_opcode           *op;

        i = 0;
        sign = 0;
        f = 0;
next:
        /* Skip blanks */
        while (isspace(*cptr))
            cptr++;
        /* Grab opcode */
        cptr = get_glyph(cptr, opcode, ',');

        if ((op = find_opcode(opcode, base_ops)) != 0) {
            d |= (t_uint64) op->opbase << 12;
        } else {
            return STOP_UUO;
        }

        cptr = get_glyph(cptr, opcode, ',');
        tag = parse_addr(&cpu_dev, opcode, &arg);
        if (*arg != opcode[0])
            d += (t_value)tag;
        if (*cptr == ',') {
            d <<= 18;
            cptr++;
            goto next;
        }
        if (*cptr != '\0')
            return STOP_UUO;
        *val = d;
        return SCPE_OK;
    } else if (sw & SWMASK('C')) {
        i = 0;
        while (*cptr != '\0' && i < 6) {
            d <<= 6;
            if (sim_ascii_to_six[0177 & *cptr] != (const char)-1)
                d |= sim_ascii_to_six[0177 & *cptr];
            cptr++;
            i++;
        }
        while (i < 6) {
            d <<= 6;
            d |= 060;
            i++;
        }
    } else {
        if (*cptr == '-') {
            sign = 1;
            cptr++;
        } else {
            sign = 0;
            if (*cptr == '+')
                cptr++;
        }
        while (*cptr >= '0' && *cptr <= '7') {
            d <<= 3;
            d |= *cptr++ - '0';
        }
        if (sign)
            d |= 00400000000000L;
    }
    *val = d;
    return SCPE_OK;
}
