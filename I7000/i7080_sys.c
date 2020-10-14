/* i7090_sys.c: IBM 705 Simulator system interface.

   Copyright (c) 2006-2016, Richard Cornwell

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

#include "i7080_defs.h"
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

char                sim_name[] = "IBM 7080";

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 50;

#ifdef NUM_DEVS_CDP
extern DEVICE       stack_dev[];
#endif

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &chan_dev,
#if NUM_DEVS_CDR > 0
    &cdr_dev,
#endif
#if NUM_DEVS_CDP > 0
    &cdp_dev,
#endif
#ifdef STACK_DEV
    &stack_dev,
#endif
#if NUM_DEVS_LPR > 0
    &lpr_dev,
#endif
#if NUM_DEVS_CON > 0
    &con_dev,
#endif
#if NUM_DEVS_MT > 0
    &mta_dev,
#if NUM_DEVS_MT > 1
    &mtb_dev,
#if NUM_DEVS_MT > 2
    &mtc_dev,
#if NUM_DEVS_MT > 3
    &mtd_dev,
#endif
#endif
#endif
#endif
#if NUM_DEVS_DR > 0
    &drm_dev,
#endif
#if NUM_DEVS_HT > 0
    &hta_dev,
#if NUM_DEVS_HT > 1
    &htb_dev,
#endif
#endif
#if NUM_DEVS_DSK > 0
    &dsk_dev,
#endif
#if NUM_DEVS_COM > 0
    &coml_dev,
    &com_dev,
#endif
#if NUM_DEVS_CHRON > 0
    &chron_dev,
#endif
    NULL
};

/* Device addressing words */
#ifdef NUM_DEVS_CDP
DIB  cdp_dib = { CH_TYP_UREC, 1, 0x300, 0xff00, &cdp_cmd, &cdp_ini };
#endif
#ifdef NUM_DEVS_CDR
DIB  cdr_dib = { CH_TYP_UREC, 1, 0x100, 0xff00, &cdr_cmd, NULL };
#endif
#ifdef NUM_DEVS_LPR
DIB  lpr_dib = { CH_TYP_UREC, 1, 0x400, 0xff00, &lpr_cmd, &lpr_ini };
#endif
#ifdef NUM_DEVS_CON
DIB  con_dib = { CH_TYP_UREC, 1, 0x500, 0xff00, &con_cmd, &con_ini };
#endif
#ifdef NUM_DEVS_DR
DIB  drm_dib = { CH_TYP_UREC, 1, 0x1000, 0xff00, &drm_cmd, &drm_ini };
#endif
#ifdef NUM_DEVS_MT
DIB  mt_dib = { CH_TYP_76XX|CH_TYP_754, NUM_UNITS_MT, 0x200, 0xff00, &mt_cmd, &mt_ini };
#endif
#ifdef NUM_DEVS_CHRON
DIB  chron_dib = { CH_TYP_76XX|CH_TYP_UREC, 1, 0x200, 0xff00, &chron_cmd, NULL };
#endif
#ifdef NUM_DEVS_HT
DIB  ht_dib = { CH_TYP_79XX, NUM_UNITS_HT, 0, 0, &ht_cmd, NULL };
#endif
#ifdef NUM_DEVS_DSK
DIB  dsk_dib = { CH_TYP_79XX, 0, 0, 0, &dsk_cmd, &dsk_ini };
#endif
#ifdef NUM_DEVS_COM
DIB  com_dib = { CH_TYP_79XX, 0, 0, 0, &com_cmd, NULL };
#endif

/* Simulator stop codes */
const char         *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "IO device not ready",
    "HALT instruction",
    "Breakpoint",
    "Unknown Opcode",
    "Error1",   /* Ind limit */ /* Not on 7080 */
    "Error2",   /* XEC limit */ /* Not on 7080 */
    "I/O Check opcode",
    "Machine Check",    /* MM in trap */
    "7750 invalid line number",
    "7750 invalid message",
    "7750 No free output buffers",
    "7750 No free input buffers",
    "Overflow Check",   /* Field overflow */
    "Sign Check",       /* Sign change */
    "Divide error",
    "Error6",   /* Alpha index */ /* Not on 7080 */
    "No word mark",
    "Invalid Address",
    "Record Check",
    "Program Check",
    "Protect Check",
     0,
};


/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CHANNEL", DEBUG_CHAN},
    {"TRAP", DEBUG_TRAP},
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {"SENSE", DEBUG_SNS},
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


const char          mem_to_ascii[64] = {
    'a', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '=', '\'', ':', '>', 's',
    ' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '#', ',', '(', '`', '\\', '_',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '^',
    '+', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '|',
                      /*Sq*/          /*GM*/
};

t_stat parse_sym(CONST char *, t_addr, UNIT *, t_value *, int32);



/* Load BCD card image into memory, following 705 standard load format */
int
load_rec(uint8 *image) {
    extern uint8       bcd_bin[16];
    extern uint32      IC;
    uint32             addr;
    int                len, i;

    /* Convert blanks to space code */
    for(i = 0; i < 80; i++)
        if (image[i] == 0)
            image[i] = 020;

    addr = bcd_bin[image[12] & 0xf];
    addr += 10 * bcd_bin[image[11] & 0xf];
    addr += 100 * bcd_bin[image[10] & 0xf];
    addr += 1000 * bcd_bin[image[9] & 0xf];
    i = (image[9] & 060) >> 4;          /* Handle zones */
    i |= (image[12] & 040) >> 3;
    i |= (image[12] & 020) >> 1;
    addr += 10000 * i;
    while(addr > EMEMSIZE) addr -= EMEMSIZE;    /* Wrap around */
    len = bcd_bin[image[14] & 0xf];
    len += 10 * bcd_bin[image[13] & 0xf];
    if (len > 65)
        len = 65;
    if (len == 0) {
        IC = addr;
        return 1;
    }
    for(i = 0; i < len; i++) {
        uint8   ch = image[15+i];
        if (ch == 075)
           ch = 077;
        M[addr++] = ch;
    }
    return 0;
}

/* Load a card image file into memory.  */
t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    char                buffer[160];
    int                 i, j;

    if (match_ext(fnam, "crd")) {
        uint8               image[80];

        while (sim_fread(buffer, 1, 160, fileref) == 160) {
            /* Convert bits into image */
            for (j = i = 0; j < 80; j++) {
                uint16  x;
                x = buffer[i++];
                x |= buffer[i++] << 8;
                image[j] = sim_hol_to_bcd(x);
            }
            if (load_rec(image))
                return SCPE_OK;
        }
        return SCPE_OK;
    } else if (match_ext(fnam, "cbn")) {
        uint8               image[80];

        while (sim_fread(buffer, 1, 160, fileref) == 160) {
            /* Convert bits into image */
            for (j = i = 0; j < 80; j++) {
                uint16  x;
                x = buffer[i++];
                x |= buffer[i++] << 8;
                image[j] = sim_hol_to_bcd(x);
            }
            if (load_rec(image))
                return SCPE_OK;
        }
        return SCPE_OK;
     } else if (match_ext(fnam, "dck")) {
        while (fgets(buffer, 160, fileref) != 0) {
            uint8               image[80];
            /* Convert bits into image */
            memset(image, 0, sizeof(image));
            for (j = 0; j < 80; j++) {
                if (buffer[j] == '\n' || buffer[j] == '\0')
                    break;
                image[j] = sim_ascii_to_six[buffer[j]&0177];
            }
            if (load_rec(image))
                return SCPE_OK;
        }
        return SCPE_OK;
    } else
        return SCPE_ARG;



    return SCPE_ARG;
}

/* Symbol tables */
typedef struct _opcode
{
    uint32              opbase;
    const char         *name;
    uint8               type;
}
t_opcode;

const char *chname[11] = {
    "*", "20", "21", "22", "23", "40", "41", "44", "45", "46", "47"
};

#define TYPE_A          1       /* Standard memory operation */
#define TYPE_B          2       /* ASU encoded operation */
#define TYPE_C          3       /* MA encoded operation  MA < 100 */
#define TYPE_D          4       /* MA + ASU fixed  MA < 100 */

t_opcode optbl[] = {
        {OP_ADD,        "ADD",  TYPE_A},
        {OP_RAD,        "RAD",  TYPE_A},
        {OP_SUB,        "SUB",  TYPE_A},
        {OP_RSU,        "RSU",  TYPE_A},
        {OP_MPY,        "MPY",  TYPE_A},
        {OP_DIV,        "DIV",  TYPE_A},
        {OP_ST,         "ST",   TYPE_A},
        {OP_ADM,        "ADM",  TYPE_A},
        {OP_AAM,        "AAM",  TYPE_A},
        {OP_SGN,        "SGN",  TYPE_A},
        {OP_SET,        "SET",  TYPE_A},
        {OP_SHR,        "SHR",  TYPE_A},
        {OP_LEN,        "LNG",  TYPE_A},
        {OP_RND,        "RND",  TYPE_A},
        {OP_LOD,        "LOD",  TYPE_A},
        {OP_UNL,        "UNL",  TYPE_A},
        {OP_LDA,        "LDA",  TYPE_A},
        {OP_ULA,        "ULA",  TYPE_A},
        {OP_SPR,        "SPR",  TYPE_A},
        {OP_RCV,        "RCV",  TYPE_A},
        {OP_SND,        "SND",  TYPE_A},
        {OP_CMP,        "CMP",  TYPE_A},
        {OP_TRE,        "TRE",  TYPE_A},
        {OP_TRH,        "TRH",  TYPE_A},
        {OP_NTR,        "NTR",  TYPE_A},
        {OP_TRP,        "TRP",  TYPE_A},
        {OP_TRZ,        "TRZ",  TYPE_A},
        {OP_NOP,        "NOP",  TYPE_A},
        {OP_TR|000100,  "TSL",  TYPE_B},
        {OP_TR,         "TR",   TYPE_A},
        {OP_TRA|000100, "TAA",  TYPE_B},
        {OP_TRA|000200, "TAB",  TYPE_B},
        {OP_TRA|000300, "TAC",  TYPE_B},
        {OP_TRA|000400, "TAD",  TYPE_B},
        {OP_TRA|000500, "TAE",  TYPE_B},
        {OP_TRA|000600, "TAF",  TYPE_B},
        {OP_TRA|000700, "TNS",  TYPE_B},
        {OP_TRA,        "TRA",  TYPE_A},
        {OP_TRS|000100, "TRR",  TYPE_B},
        {OP_TRS|000200, "TTC",  TYPE_B},
        {OP_TRS|000300, "TSA",  TYPE_B},
        {OP_TRS|001100, "TAR",  TYPE_B},
        {OP_TRS|001200, "TIC",  TYPE_B},
        {OP_TRS|001300, "TMC",  TYPE_B},
        {OP_TRS|001400, "TRC",  TYPE_B},
        {OP_TRS|001500, "TEC",  TYPE_B},
        {OP_TRS|001600, "TOC",  TYPE_B},
        {OP_TRS|001700, "TSC",  TYPE_B},
        {OP_TRS,        "TRS",  TYPE_A},
        {OP_TMT,        "TMT",  TYPE_A},
        {OP_CTL2|000000,"SPC",  TYPE_B},
        {OP_CTL2|000200,"LFC",  TYPE_B},
        {OP_CTL2|000300,"UFC",  TYPE_B},
        {OP_CTL2|000400,"LSB",  TYPE_B},
        {OP_CTL2|000500,"USB",  TYPE_B},
        {OP_CTL2|000600,"EIM",  TYPE_B},
        {OP_CTL2|000700,"LIM",  TYPE_B},
        {OP_CTL2|001000,"TCT",  TYPE_B},
        {OP_CTL2|001100,"B",    TYPE_B},
        {OP_CTL2|001200,"EIA",  TYPE_B},
        {OP_CTL2|001300,"CNO",  TYPE_B},
        {OP_CTL2|001400,"TLU",  TYPE_B},
        {OP_CTL2|001500,"TLH",  TYPE_B},
        {OP_CTL2|001600,"TIP",  TYPE_B},
        {OP_CTL2|001700,"LIP",  TYPE_B},
        {OP_CTL2,       "CTL2", TYPE_A},
        {OP_BLM|000100, "BLMS", TYPE_B},
        {OP_BLM,        "BLM",  TYPE_A},
        {OP_SEL,        "SEL",  TYPE_A},
        {OP_CTL|001400, "ECB",  TYPE_B},
        {OP_CTL|001500, "CHR",  TYPE_B},
        {OP_CTL|001600, "EEM",  TYPE_B},
        {OP_CTL|001700, "LEM",  TYPE_B},
        {OP_CTL|0010000, "WTM",  TYPE_D},
        {OP_CTL|0020100, "RUN",  TYPE_D},
        {OP_CTL|0020000, "RWD",  TYPE_D},
        {OP_CTL|0030000, "ION",  TYPE_D},
        {OP_CTL|0040100, "BSF",  TYPE_D},
        {OP_CTL|0040000, "BSP",  TYPE_D},
        {OP_CTL|0050000, "SUP",  TYPE_C},
        {OP_CTL|0110000, "SKP",  TYPE_C},
        {OP_CTL|0450000, "SDL",  TYPE_C},
        {OP_CTL|0460000, "SDH",  TYPE_C},
        {OP_CTL|0000000, "IOF",  TYPE_D},
        {OP_CTL,        "CTL",  TYPE_A},
        {OP_HLT,        "HLT",  TYPE_A},
        {OP_WR|000500,  "WMC",  TYPE_B},
        {OP_WR|000400,  "CWR",  TYPE_B},
        {OP_WR|000300,  "SCC",  TYPE_B},
        {OP_WR|000200,  "SRC",  TYPE_B},
        {OP_WR|000100,  "DMP",  TYPE_B},
        {OP_WR,         "WR",   TYPE_A},
        {OP_RWW,        "RWW",  TYPE_A},
        {OP_RD|000500,  "RMB",  TYPE_B},
        {OP_RD|000400,  "CRD",  TYPE_B},
        {OP_RD|000300,  "SST",  TYPE_B},
        {OP_RD|000200,  "RMA",  TYPE_B},
        {OP_RD|000100,  "FSP",  TYPE_B},
        {OP_RD,         "RD",   TYPE_A},
        {OP_WRE|000100, "WRZ",  TYPE_B},
        {OP_WRE,        "WRE",  TYPE_A},
        {OP_SBZ|000100, "SBZ1", TYPE_B},
        {OP_SBZ|000200, "SBZ2", TYPE_B},
        {OP_SBZ|000300, "SBZ3", TYPE_B},
        {OP_SBZ|000400, "SBZ4", TYPE_B},
        {OP_SBZ|000500, "SBZ5", TYPE_B},
        {OP_SBZ|000600, "SBZ6", TYPE_B},
        {OP_SBZ|000700, "SBA",  TYPE_B},
        {OP_SBZ|001000, "SBR",  TYPE_B},
        {OP_SBZ|001100, "SBN1", TYPE_B},
        {OP_SBZ|001200, "SBN2", TYPE_B},
        {OP_SBZ|001300, "SBN3", TYPE_B},
        {OP_SBZ|001400, "SBN4", TYPE_B},
        {OP_SBZ|001500, "SBN5", TYPE_B},
        {OP_SBZ|001600, "SBN6", TYPE_B},
        {OP_SBZ,        "SBZ",  TYPE_A},
        {OP_TZB,        "TZB",  TYPE_A},
        {OP_SMT|001600, "SMT",  TYPE_A},
        {0,             NULL,   0},
};


/* Print out a address plus index */
t_stat fprint_addr (FILE *of, uint32 addr) {
    fprintf(of, "%d", addr);
    return SCPE_OK;
}

/* Register change decode

   Inputs:
        *of     =       output stream
        inst    =       mask bits
*/

t_stat
fprint_reg (FILE *of, uint32 rdx, t_value *val, UNIT *uptr, int32 sw)
{
    fprintf(of, "Register(%d, %x)", rdx, *val);
    return SCPE_OK;
}



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

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
int32   i, t;
uint8   op;

if (sw & SIM_SW_REG)
    return fprint_reg(of, addr, val, uptr, sw);

if (sw & SWMASK ('C')) {                                /* character? */
    t = val[0];
    fprintf (of, " %c<%02o> ", mem_to_ascii[t & 077], t & 077);
    return SCPE_OK;
    }
if ((uptr != NULL) && (uptr != &cpu_unit)) return SCPE_ARG;     /* CPU? */
if (sw & SWMASK ('D')) {                                /* dump? */
    for (i = 0; i < 50; i++) fprintf (of, "%c", mem_to_ascii[val[i]&077]) ;
    return -(i - 1);
    }
if (sw & SWMASK ('S')) {                                /* string? */
    i = 0;
    do {
        t = val[i++];
        fprintf (of, "%c", mem_to_ascii[t & 077]);
    } while (i < 50);
    return -(i - 1);
    }
if (sw & SWMASK ('M')) {                                /* machine code? */
    uint32      addr;
    t_opcode    *tab;
    uint8       zone;
    uint8       reg;
    uint16      opvalue;

    i = 0;
    op = val[i++] & 077;
    t = val[i++];       /* First address char */
    zone = (t & 060) >> 4;
    t &= 0xf;
    if (t == 10)
        t = 0;
    addr = t * 1000;
    t = val[i++];       /* Second address char */
    reg = (t & 060) >> 2;
    t &= 0xf;
    if (t == 10)
        t = 0;
    addr += t * 100;
    t = val[i++];       /* Third address char */
    reg |= (t & 060) >> 4;
    t &= 0xf;
    if (t == 10)
        t = 0;
    addr += t * 10;
    t = val[i++];       /* Forth address char */
    zone |= (t & 060) >> 2;
        /* Switch BA bits in high zone */
    zone = (zone & 03) | ((zone & 04) << 1) | ((zone & 010) >> 1);
    t &= 0xf;
    if (t == 10)
        t = 0;
    addr += t;
    opvalue = op | (reg << 6);
    addr += zone * 10000;
    for(tab = optbl; tab->name != NULL; tab++) {
        if (tab->type == TYPE_A && op == tab->opbase)
            break;
        if (tab->type == TYPE_B && opvalue == tab->opbase)
            break;
        if (tab->type == TYPE_C && addr < 100 &&
                 (op|(addr << 12)) == tab->opbase)
            break;
        if (tab->type == TYPE_D && addr < 100 &&
                 (opvalue|(addr << 12)) == tab->opbase)
            break;
    }

    if (tab->name == NULL)
        fprintf(of, "%c<%02o>\t", mem_to_ascii[op], op);
    else
        fprintf(of, "%s\t", tab->name);

    switch(tab->type) {
    case TYPE_A:
        fprintf(of, "%d", addr);
        if (reg != 0)
            fprintf(of, ",%d", reg);
        break;
    case TYPE_B:
        fprintf(of, "%d", addr);
        break;
    case TYPE_C:        /* No operand required for type C or D */
    case TYPE_D:
        break;
    }
    return -(i - 1);
}
fprintf (of, " %02o ", val[0] & 077);
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
    t_value             d;
    char                buffer[100];

    while (isspace(*cptr))
        cptr++;
    d = 0;
    i = 0;
    if (sw & SWMASK('C')) {
        while (*cptr != '\0') {
            d = sim_ascii_to_six[0177 & *cptr++];
            if (d == 0)
                d = 020;
            val[i++] = d;
        }
        if (i == 0)
            return SCPE_ARG;
        return -(i - 1);
    } else if (sw & SWMASK('M')) {
        t_opcode           *op;
        uint32             addr = 0;
        uint8              asu = 0;
        uint8              zone;
        uint8              t;

        i = 0;
        /* Grab opcode */
        cptr = get_glyph(cptr, buffer, 0);
        if ((op = find_opcode(buffer, optbl)) == 0)
            return STOP_UUO;
        if (op->type == TYPE_C || op->type == TYPE_D) {
            addr = op->opbase >> 12;
            val[i++] = op->opbase & 077;
            val[i++] = 10;
            val[i++] = ((op->opbase & 01400) >> 4) | 10;
            t = addr / 10;
            if (t == 0)
                t = 10;
            val[i++] = ((op->opbase & 00300) >> 2) | t;
            t = addr % 10;
            if (t == 0)
                t = 10;
            val[i++] = t;
            return -(i - 1);
        }
        /* Skip blanks */
        while(isspace(*cptr))   cptr++;
        /* Collect address */
        while(*cptr >= '0' && *cptr <= '9')
            addr = (addr * 10) + (*cptr++ - '0');
        /* Skip blanks */
        while(isspace(*cptr))   cptr++;
        if (*cptr == ',') {     /* Collect a ASU */
            while(*cptr >= '0' && *cptr <= '9')
                asu = (asu * 10) + (*cptr++ - '0');

        }
        /* Skip blanks */
        while(isspace(*cptr))   cptr++;
        if (*cptr != '\0')
            return SCPE_ARG;
        /* Type B's can't have ASU */
        if (op->type == TYPE_B) {
            if (asu != 0)
                return STOP_UUO;
            asu = (op->opbase >> 6) & 017;
        }
        /* Check if ASU out of range */
        if (asu > 16)
            return SCPE_ARG;
        zone = addr / 10000;
        if (zone > 16)
            return SCPE_ARG;
        addr %= 10000;
        val[i++] = op->opbase & 077;
        t = addr / 1000;
        if (t == 0)
            t = 10;
        addr %= 1000;
        val[i++] = t | ((zone << 4) & 060);
        t = addr / 100;
        if (t == 0)
            t = 10;
        addr %= 100;
        val[i++] = t | ((asu << 2) & 060);
        t = addr / 10;
        if (t == 0)
            t = 10;
        addr %= 10;
        val[i++] = t | ((asu << 4) & 060);
        t = addr;
        if (t == 0)
            t = 10;
        addr %= 10;
        val[i++] = t | ((zone << 2) & 060);
        return -(i - 1);
    } else {
        int     sign = 0;

        i = 0;
        while (*cptr != '\0') {
            sign = 0;
            /* Skip blanks */
            while(isspace(*cptr))       cptr++;
            if (*cptr == '+') {
                cptr++;
                sign = 1;
            } else if (*cptr == '-') {
                cptr++;
                sign = -1;
            }
            if (!(*cptr >= '0' && *cptr <= '9'))
                return SCPE_ARG;
            while(*cptr >= '0' && *cptr <= '9') {
                d = *cptr++ - '0';
                if (d == 0)
                    d = 10;
                val[i++] = d;
            }
            if (*cptr == ',')
                cptr++;
            if(sign)
                val[i-1] |= (sign==-1)?040:060; /* Set sign last digit */
        }
        if (i == 0)
            return SCPE_ARG;
        return -(i - 1);
    }
    return SCPE_OK;
}
