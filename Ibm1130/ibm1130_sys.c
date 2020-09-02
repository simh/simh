/* ibm1130_sys.c: IBM 1130 simulator interface

   Based on PDP-11 simulator written by Robert M Supnik

   Revision History
   0.27 2005Mar08 - Added sca device
   0.26 2002Apr24 - Added !BREAK in card deck file to stop simulator
   0.25 2002Apr18 - Fixed some card reader problems. It starts the reader
                    properly if you attach a deck while it's waiting to a read.
   0.24 2002Mar27 - Fixed BOSC bug; BOSC works in short instructions too
   0.23 2002Feb26 - Added @decklist feature for ATTACH CR.
   0.22 2002Feb26 - Replaced "strupr" with "upcase" for compatibility.
   0.21 2002Feb25 - Some compiler compatibiity changes, couple of compiler-detected
                    bugs
   0.01 2001Jul31 - Derived from pdp11_sys.c, which carries this disclaimer:

 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org
 */

#include "ibm1130_defs.h"
#include <ctype.h>
#include <stdarg.h>

extern DEVICE cpu_dev, console_dev, dsk_dev, cr_dev,  cp_dev, ptr_dev, ptp_dev, t2741_dev;
extern DEVICE tti_dev, tto_dev,     prt_dev, log_dev, sca_dev;
extern DEVICE gdu_dev, console_dev, plot_dev;

extern UNIT  cpu_unit;
extern REG   cpu_reg[];
extern int32 saved_PC;
extern t_bool is_1800;

/* SCP data structures and interface routines

   sim_name     simulator name string
   sim_PC       pointer to saved PC register descriptor
   sim_emax     number of words for examine
   sim_devices      array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load     binary loader
*/

char sim_name[]    = "IBM 1130";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
    &cpu_dev,           /* the cpu */
    &dsk_dev,           /* disk drive(s) */
    &cr_dev,            /* card reader/punch */
    &cp_dev,
    &tti_dev,           /* console keyboard, selectric printer */
    &tto_dev,
    &prt_dev,           /* 1132 printer */
    &ptr_dev,           /* 1134 paper tape reader */
    &ptp_dev,           /* 1055 paper tape punch */
    &sca_dev,           /* Synchronous communications adapter option */
    &console_dev,       /* console display (windows GUI) */
    &gdu_dev,           /* 2250 display */
    &t2741_dev,         /* nonstandard serial interface used by APL\1130 */
    &plot_dev,          /* plotter device, in ibm1130_plot.c */
    NULL
};

const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "Wait",
    "Invalid command", 
    "Simulator breakpoint",
    "Use of incomplete simulator function",
    "Power off",
    "!BREAK in card deck file",
    "Phase load break",
    "Program has run amok",
    "Run time limit exceeded",
    "Immediate Stop key requested",
    "Simulator break key pressed",
    "Simulator step count expired",
    "Simulator IO error",
};

/* Loader. IPL is normally performed by card reader (boot command). This function
 * loads hex data from a file for testing purposes. The format is:
 *
 *   blank lines or lines starting with ; / or # are ignored as comments
 *
 *   @XXXX          set load addresss to hex value XXXX
 *   XXXX           store hex word value XXXX at current load address and increment address
 *   ...
 *   =XXXX          set IAR to hex value XXXX
 *   ZXXXX          zero XXXX words and increment load address
 *   SXXXX          set console entry switches to XXXX. This lets a program specify the
 *                  default value for the toggle switches.
 *
 * Multiple @ and data sections may be entered. If more than one = or S value is specified
 * the last one wins.
 *
 * Note: the load address @XXXX and data values XXXX can be followed by the letter
 * R to indicate that the values are relocatable addresses. This is ignored in this loader,
 * but the asm1130 cross assembler may put them there.
 */

t_stat my_load (FILE *fileref, const char *cptr, const char *fnam)
{
    char line[150], *c;
    int iaddr = -1, runaddr = -1, val, nwords;

    while (fgets(line, sizeof(line), fileref) != NULL) {
        for (c = line; *c && *c <= ' '; c++)            /* find first nonblank */
            ;

        if (*c == '\0' || *c == '#' || *c == '/' || *c == ';')
            continue;                                   /* empty line or comment */

        if (*c == '@') {                                /* set load address */
            if (sscanf(c+1, "%x", &iaddr) != 1)
                return SCPE_FMT;
        }
        else if (*c == '=') {
            if (sscanf(c+1, "%x", &runaddr) != 1)
                return SCPE_FMT;
        }
        else if (*c == 's' || *c == 'S') {
            if (sscanf(c+1, "%x", &val) != 1)
                return SCPE_FMT;

            CES = val & 0xFFFF;                         /*preload console entry switches */
        }
        else if (*c == 'z' || *c == 'Z') {
            if (sscanf(c+1, "%x", &nwords) != 1)
                return SCPE_FMT;

            if (iaddr == -1)
                return SCPE_FMT;

            while (--nwords >= 0) {
                WriteW(iaddr, 0);
                iaddr++;
            }
        }
        else if (strchr("0123456789abcdefABCDEF", *c) != NULL) {
            if (sscanf(c, "%x", &val) != 1)
                return SCPE_FMT;

            if (iaddr == -1)
                return SCPE_FMT;

            WriteW(iaddr, val);                         /*store data */
            iaddr++;
        }
        else
            return SCPE_FMT;                            /*unexpected data */
    }

    if (runaddr != -1)
        IAR = runaddr;

    return SCPE_OK;
}

t_stat my_save (FILE *fileref, const char *cptr, const char *fnam)
{
    int iaddr, nzeroes = 0, nwords = (int) (MEMSIZE/2), val;

    fprintf(fileref, "=%04x\r\n", IAR);
    fprintf(fileref, "@0000\r\n");
    for (iaddr = 0; iaddr < nwords; iaddr++) {
        val = ReadW(iaddr);
        if (val == 0)                       /*queue up zeroes */
            nzeroes++;
        else {
            if (nzeroes >= 4) {             /*spit out a Z directive */
                fprintf(fileref, "Z%04x\r\n", nzeroes);
                nzeroes = 0;
            }
            else {                          /*write queued zeroes literally */
                while (nzeroes > 0) {
                    fprintf(fileref, " 0000\r\n");
                    nzeroes--;
                }
            }
            fprintf(fileref, " %04x\r\n", val);
        }
    }
    if (nzeroes >= 4) {                     /*emit any queued zeroes */
        fprintf(fileref, "Z%04x\r\n", nzeroes);
        nzeroes = 0;
    }
    else {
        while (nzeroes > 0) {
            fprintf(fileref, " 0000\r\n");
            nzeroes--;
        }
    }

    return SCPE_OK;
}

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    if (flag)
        return my_save(fileref, cptr, fnam);
    else
        return my_load(fileref, cptr, fnam);
}

/* Specifier decode

   Inputs:
    *of =   output stream
    addr    =   current PC
    spec    =   specifier
    nval    =   next word
    flag    =   TRUE if decoding for CPU
    iflag   =   TRUE if decoding integer instruction
   Outputs:
    count   =   -number of extra words retired
*/

/* Symbolic decode

   Inputs:
    *of =   output stream
    addr    =   current PC
    *val    =   values to decode
    *uptr   =   pointer to unit
    sw  =   switches
   Outputs:
    return  =   if >= 0, error code
            if < 0, number of extra words retired
*/

static const char *opcode[] = {
    "?00 ",     "XIO ",     "SLA ",     "SRA ",
    "LDS ",     "STS ",     "WAIT",     "?07 ",
    "BSI ",     "BSC ",     "?0A ",     "?0B ",
    "LDX ",     "STX ",     "MDX ",     "?0F ",
    "A   ",     "AD  ",     "S   ",     "SD  ",
    "M   ",     "D   ",     "?16 ",     "?17 ",
    "LD  ",     "LDD ",     "STO ",     "STD ",
    "AND ",     "OR  ",     "EOR ",     "?1F ",
};

static char relative[] = {                      /*true if short mode displacements are IAR relative */
    FALSE,      TRUE,       FALSE,      FALSE,
    FALSE,      TRUE,       FALSE,      FALSE,
    TRUE,       FALSE,      FALSE,      FALSE,
    TRUE,       TRUE,       TRUE,       FALSE,
    TRUE,       TRUE,       TRUE,       TRUE,
    TRUE,       TRUE,       FALSE,      FALSE,
    TRUE,       TRUE,       TRUE,       TRUE,
    TRUE,       TRUE,       TRUE,       FALSE
};

static const char *lsopcode[] = {"SLA ", "SLCA ", "SLT ", "SLC "};
static const char *rsopcode[] = {"SRA ", "?188 ", "SRT ", "RTE "};
static const char tagc[]      = " 123";

static int ascii_to_ebcdic_table[128] = 
{
    0x00,0x01,0x02,0x03,0x37,0x2d,0x2e,0x2f, 0x16,0x05,0x25,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x3c,0x3d,0x32,0x26, 0x18,0x19,0x3f,0x27,0x1c,0x1d,0x1e,0x1f,
    0x40,0x5a,0x7f,0x7b,0x5b,0x6c,0x50,0x7d, 0x4d,0x5d,0x5c,0x4e,0x6b,0x60,0x4b,0x61,
    0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7, 0xf8,0xf9,0x7a,0x5e,0x4c,0x7e,0x6e,0x6f,

    0x7c,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7, 0xc8,0xc9,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,
    0xd7,0xd8,0xd9,0xe2,0xe3,0xe4,0xe5,0xe6, 0xe7,0xe8,0xe9,0xba,0xe0,0xbb,0xb0,0x6d,
    0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87, 0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
    0x97,0x98,0x99,0xa2,0xa3,0xa4,0xa5,0xa6, 0xa7,0xa8,0xa9,0xc0,0x4f,0xd0,0xa1,0x07,
};

static int ebcdic_to_ascii (int ch)
{
    int j;

    for (j = 32; j < 128; j++)
        if (ascii_to_ebcdic_table[j] == ch)
            return j;

    return '?';
}

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
    int32 ch, OP, F, TAG, INDIR, DSPLC, IR, eaddr;
    const char *mnem;
    char tst[12];

/*  if (sw & SWMASK ('A')) {                    // ASCII? not useful
        fprintf (of, (c1 < 040)? "<%03o>": "%c", c1);
        return SCPE_OK;
    }
*/

    if (sw & SWMASK ('C'))                  /* character? not useful -- make it EBCDIC */
        sw |= SWMASK('E');

    if (sw & SWMASK ('E')) {                /* EBCDIC! */
        ch = ebcdic_to_ascii((val[0] >> 8) & 0xFF); /* take high byte first */
        fprintf (of, (ch < ' ')? "<%03o>": "%c", ch);
        ch = ebcdic_to_ascii(val[0] & 0xFF);
        fprintf (of, (ch < ' ')? "<%03o>": "%c", ch);
        return SCPE_OK;
    }

    if (sw & SWMASK ('H')) {                /* HOLLERITH! now THIS is useful! */
        ch = hollerith_to_ascii((int16) val[0]);
        fprintf (of, (ch < ' ')? "<%03o>": "%c", ch);
        return SCPE_OK;
    }

    if (! (sw & SWMASK ('M')))
        return SCPE_ARG;

    IR  = val[0];
    OP  = (IR >> 11) & 0x1F;            /* opcode */
    F   = IR & 0x0400;                  /* format bit: 1 = long instr */
    TAG = IR & 0x0300;                  /* tag bits: index reg select */
    if (TAG)
        TAG >>= 8;

    if (F) {                            /* long instruction, ASSUME it's valid (have to decrement IAR if not) */
        INDIR = IR & 0x0080;            /* indirect bit */
        DSPLC = IR & 0x007F;            /* displacement or modifier */
        if (DSPLC & 0x0040)
            DSPLC |= ~ 0x7F;            /* sign extend */

        eaddr = val[1];                 /* get reference address */
    }
    else {                              /* short instruction, use displacement */
        INDIR = 0;                      /* never indirect */
        DSPLC = IR & 0x00FF;            /* get displacement */
        if (DSPLC & 0x0080)
            DSPLC |= ~ 0xFF;

        eaddr = DSPLC;
        if (relative[OP] && ! TAG)
            eaddr += addr+1;            /* turn displacement into address */
    }

    mnem = opcode[OP];                  /* get mnemonic */
    if (is_1800) {                      /* these two are defined on the 1800 but undefined on the 1130 */
        if (OP == 0x16)
            mnem = "CMP ";
        else if (OP == 0x17)
            mnem = "DCMP";
    }

    if (OP == 0x02) {                   /* left shifts are special */
        mnem = lsopcode[(DSPLC >> 6) & 0x0003];
        DSPLC &= 0x003F;
        eaddr = DSPLC;
    }
    else if (OP == 0x03) {              /* right shifts too */
        mnem = rsopcode[(DSPLC >> 6) & 0x0003];
        DSPLC &= 0x003F;
        eaddr = DSPLC;
    }
    else if ((OP == 0x08 && F)|| OP == 0x09) {      /* BSI L and BSC any */
        if (OP == 0x09 && (IR & 0x40))
            mnem = "BOSC";

        tst[0] = '\0';
        if (DSPLC & 0x20)   strcat(tst, "Z");
        if (DSPLC & 0x10)   strcat(tst, "-");
        if (DSPLC & 0x08)   strcat(tst, "+");
        if (DSPLC & 0x04)   strcat(tst, "E");
        if (DSPLC & 0x02)   strcat(tst, "C");
        if (DSPLC & 0x01)   strcat(tst, "O");

        if (F) {
            fprintf(of, "%04x %s %c%c %s,%04x   ", IR & 0xFFFF, mnem, F ? (INDIR ? 'I' : 'L') : ' ', tagc[TAG], tst, eaddr & 0xFFFF);
            return -1;
        }
        fprintf(of, "%04x %s %c%c %s   ", IR & 0xFFFF, mnem, F ? (INDIR ? 'I' : 'L') : ' ', tagc[TAG], tst);
        return SCPE_OK;
    }
    else if (OP == 0x0e && TAG == 0) {      /* MDX with no tag => MDM or jump */
        if (F) {
            fprintf(of, "%04x %s %c%c %04x,%x (%d)   ", IR & 0xFFFF, "MDM ", (INDIR ? 'I' : 'L'), tagc[TAG], eaddr & 0xFFFF, DSPLC & 0xFFFF, DSPLC);
            return -1;
        }
        mnem = "JMP ";
    }

    fprintf(of, "%04x %s %c%c %04x   ", IR & 0xFFFF, mnem, F ? (INDIR ? 'I' : 'L') : ' ', tagc[TAG], eaddr & 0xFFFF);
    return F ? -1 : SCPE_OK;            /* inform how many words we read */
}

int32 get_reg (char *cptr, const char *strings[], char mchar)
{
return -1;
}

/* Number or memory address

   Inputs:
    *cptr   =   pointer to input string
    *dptr   =   pointer to output displacement
    *pflag  =   pointer to accumulating flags
   Outputs:
    cptr    =   pointer to next character in input string
            NULL if parsing error

   Flags: 0 (no result), A_NUM (number), A_REL (relative)
*/

char *get_addr (char *cptr, int32 *dptr, int32 *pflag)
{
    return 0;
}

/* Specifier decode

   Inputs:
    *cptr   =   pointer to input string
    addr    =   current PC
    n1  =   0 if no extra word used
            -1 if extra word used in prior decode
    *sptr   =   pointer to output specifier
    *dptr   =   pointer to output displacement
    cflag   =   true if parsing for the CPU
    iflag   =   true if integer specifier
   Outputs:
    status  =   = -1 extra word decoded
            =  0 ok
            = +1 error
*/

t_stat get_spec (char *cptr, t_addr addr, int32 n1, int32 *sptr, t_value *dptr,
    int32 cflag, int32 iflag)
{
    return -1;
}

/* Symbolic input

   Inputs:
    *cptr   =   pointer to input string
    addr    =   current PC
    *uptr   =   pointer to unit
    *val    =   pointer to output values
    sw  =   switches
   Outputs:
    status  =   > 0   error code
            <= 0  -number of extra words
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
    return SCPE_ARG;
}

#ifndef _WIN32

int strnicmp (const char *a, const char *b, size_t n)
{
    int ca, cb;

    if (n == 0) return 0;               /* zero length compare is equal */

    for (;;) {
        if ((ca = *a) == 0)             /* get character, stop on null terminator */
            return *b ? -1 : 0;

        if (ca >= 'a' && ca <= 'z')     /* fold lowercase to uppercase */
            ca -= 32;

        cb = *b;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;

        if ((ca -= cb) != 0)            /* if different, return comparison */
            return ca;

        a++, b++;

        if (--n == 0)                   /* still equal after n characters? quit now */
            return 0;
    }
}

int strcmpi (const char *a, const char *b)
{
    int ca, cb;

    for (;;) {
        if ((ca = *a) == 0)             /* get character, stop on null terminator */
            return *b ? -1 : 0;

        if (ca >= 'a' && ca <= 'z')     /* fold lowercase to uppercase */
            ca -= 32;

        cb = *b;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;

        if ((ca -= cb) != 0)            /* if different, return comparison */
            return ca;

        a++, b++;
    }
}

#endif
