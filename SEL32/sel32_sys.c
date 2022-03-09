/* sel32_sys.c: SEL-32 Gould Concept/32 (orignal SEL-32) Simulator system interface.

   Copyright (c) 2018-2022, James C. Bevier
   Portions provided by Richard Cornwell, Geert Rolf and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "sel32_defs.h"
#include <ctype.h>

extern REG cpu_reg[];
extern uint32 M[MAXMEMSIZE];
extern uint32 SPAD[];
extern uint32 PSD[];
char *dump_mem(uint32 mp, int cnt);
char *dump_buf(uint8 *mp, int32 off, int cnt);

/* SCP data structures and interface routines

The interface between the simulator control package (SCP) and the
simulator consists of the following routines and data structures

sim_name            simulator name string
sim_devices[]       array of pointers to simulated devices
sim_PC              pointer to saved PC register descriptor
sim_interval        simulator interval to next event (in sel32_cpu.c)
sim_stop_messages[] array of pointers to stop messages
sim_instr()         instruction execution routine (in sel32_cpu.c)
sim_load()          binary loader routine
sim_emax            maximum number of words for examine

In addition, the simulator must supply routines to print and parse
architecture specific formats

fprint_sym          print symbolic output
fparse_sym          parse symbolic input
*/

char sim_name[] = "SEL-32";                 /* our simulator name */
REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 4;                         /* maximum number of instructions/words to examine */

DEVICE *sim_devices[] = {
        &cpu_dev,
#ifdef NUM_DEVS_IOP
        &iop_dev,                           /* IOP channel controller */
#endif
#ifdef NUM_DEVS_MFP
        &mfp_dev,                           /* MFP channel controller */
#endif
#ifdef NUM_DEVS_RTOM
        &rtc_dev,
        &itm_dev,
#endif
#ifdef NUM_DEVS_CON
        &con_dev,
#endif
#ifdef NUM_DEVS_CDR
        &cdr_dev,
#endif
#ifdef NUM_DEVS_CDP
        &cdp_dev,
#endif
#ifdef NUM_DEVS_LPR
        &lpr_dev,
#endif
#ifdef NUM_DEVS_MT
        &mta_dev,
#if NUM_DEVS_MT > 1
        &mtb_dev,
#endif
#endif
#ifdef NUM_DEVS_DISK
        &dda_dev,
#if NUM_DEVS_DISK > 1
        &ddb_dev,
#endif
#endif
#ifdef NUM_DEVS_SCFI
        &sda_dev,
#if NUM_DEVS_SCFI > 1
        &sdb_dev,
#endif
#endif
#ifdef NUM_DEVS_HSDP
        &dpa_dev,
#if NUM_DEVS_HSDP > 1
        &dpb_dev,
#endif
#endif
#ifdef NUM_DEVS_SCSI
        &sba_dev,
#if NUM_DEVS_SCSI > 1
        &sbb_dev,
#endif
#endif
#ifdef NUM_DEVS_ETHER
        &ec_dev,
#endif
#ifdef NUM_DEVS_COM
        &coml_dev,
        &com_dev,
#endif
       NULL };

/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"INST", DEBUG_INST, "Show instruction execution"},
    {"XIO", DEBUG_XIO, "Show XIO I/O instructions"},
    {"IRQ", DEBUG_IRQ, "Show interrupt requests"},
    {"TRAP", DEBUG_TRAP, "Show trap requests"},
    {0, 0}
};

const char *sim_stop_messages[SCPE_BASE] = {
       "Unknown error",
       "IO device not ready",
       "HALT instruction",
       "Breakpoint",
       "Unknown Opcode",
       "Invalid instruction",
       "Invalid I/O operation",
       "Nested indirects exceed limit",
       "I/O Check opcode",
       "Memory management trap during trap",
};

#define PRINTABLE(x) ((x < 32) || (x > 126)) ? '.' : x

static char line[257];
/* function to dump SEL32 memory up to 16 bytes with side by side ascii values */
char *dump_mem(uint32 mp, int cnt)
{
    char    buff[257];
    uint32  ma = mp;                            /* save memory address */
    char    *cp = &line[0];                     /* output buffer */
    int     cc=0, ch, bp=0, bl=cnt;

    if (cnt > 16)
        bl = 16;                                /* stop at 16 chars */

    while (bp < bl) {
        if (!bp) {
            cc = sprintf(cp, " %06x : ", ma);    /* output location address */
            cp += cc;                           /* next print location */
        }
        ch = RMB(ma) & 0xff;                    /* get a char from memory */
        ma++;                                   /* next loc */
        cc += sprintf(cp, "%02x", ch);          /* print out current char */
        cp += 2;                                /* next print location */
        buff[bp++] = PRINTABLE(ch);             /* get printable version of char */
        if (!(bp % 4)) {                        /* word boundry yet? */
            cc += sprintf(cp, " ");             /* space between words */
            cp += 1;                            /* next print location */
        }
    }

    while (bp < 16) {
        cc += sprintf(cp, " ");                 /* print out one space */
        cp += 1;                                /* next print location */
        buff[bp++] = 0x20;                      /* blank char buffer */
        if (!(bp % 4)) {
            cc += sprintf(cp, " ");             /* space between words */
            cp += 1;                            /* next print location */
        }
    }
    buff[bp] = 0;                               /* terminate line */
    cc += sprintf(cp, "|%s|\n", buff);          /* print out ascii text */
    return (line);                              /* return pointer to caller */
}

/* function to dump caller buffer upto 16 bytes with side by side ascii values */
/* off is offset in buffer to start */
char *dump_buf(uint8 *mp, int32 off, int cnt)
{
    char    buff[257];
    uint32  ma = off;                           /* save memory address */
    char    *cp = &line[0];                     /* output buffer */
    int     cc=0, ch, bp=0, bl=cnt;

    if (cnt > 16)
        bl = 16;                                /* stop at 16 chars */

    while (bp < bl) {
        if (!bp) {
            cc = sprintf(cp, " %06x : ", ma);    /* output location offset */
            cp += cc;                           /* next print location */
        }
        ch = mp[ma++] & 0xff;                   /* get a char from memory */
        cc += sprintf(cp, "%02x", ch);          /* print out current char */
        cp += 2;                                /* next print location */
        buff[bp++] = PRINTABLE(ch);             /* get printable version of char */
        if (!(bp % 4)) {                        /* word boundry yet? */
            cc += sprintf(cp, " ");             /* space between words */
            cp += 1;                            /* next print location */
        }
    }

    while (bp < 16) {
        cc += sprintf(cp, " ");                 /* print out one space */
        cp += 1;                                /* next print location */
        buff[bp++] = 0x20;                      /* blank char buffer */
        if (!(bp % 4)) {
            cc += sprintf(cp, " ");             /* space between words */
            cp += 1;                            /* next print location */
        }
    }
    buff[bp] = 0;                               /* terminate line */
    cc += sprintf(cp, "|%s|\n", buff);          /* print out ascii text */
    return (line);                              /* return pointer to caller */
}



/*
 * get_word - function to load a 32 bit word from the input file 
 * return 1 - OK
 * return 0 - error or eof
 */
int get_word(FILE *fileref, uint32 *word)
{
    unsigned char cbuf[4];

    /* read in the 4 chars */
    if (sim_fread(cbuf, 1, 4, fileref) != 4)
        return 1;                           /* read error or eof */
    /* byte swap while reading data */
    *word = ((cbuf[0]) << 24) | ((cbuf[1]) << 16) |
            ((cbuf[2]) << 8) | ((cbuf[3]));
    return 0;                               /* all OK */
}

#ifdef NO_TAP_FOR_NOW
/*
 * get_halfword - function to load a 16 bit halfword from the input file 
 * return 1 - OK
 * return 0 - error or eof
 */
int get_halfword(FILE *fileref, uint16 *word)
{
    unsigned char cbuf[2];

    /* read in the 2 chars */
    if (sim_fread(cbuf, 1, 2, fileref) != 2)
        return 1;                           /* read error or eof */
    /* byte swap while reading data */
    *word = ((uint16)(cbuf[0]) << 8) | ((uint16)(cbuf[1]));
    return 0;                               /* all OK */
}
#endif

/* load a binary file into memory starting at loc 0 */
/* return SCPE_OK on load complete */
t_stat load_mem (FILE *fileref)
{
    uint32 data;
    uint32 ma = 0;  /* start at mem add 0 */

    /* read the file until the end */
    for ( ;; ) {
        if (get_word(fileref, &data))       /* get 32 bits of data */
            return SCPE_OK;                 /* load is complete, return */
        M[ma++] = data;                     /* put data in memory */
    }
    return SCPE_OK;                         /* never here */
}

#ifdef NO_TAP_FOR_NOW
/* load tap formated tape into memory */
/* return SCPE_OK on load complete */
t_stat load_tap (FILE *fileref)
{
    uint32 bdata, edata;
    uint16 hdata;
    uint32 ma = 0;                          /* start loading at loc 0 */
    int32 wc;

    for ( ;; ) {                            /* loop until EOF read */
        /* look for record byte count or zero for EOF */
        if (get_word(fileref, &bdata))      /* read 4 bytes of data */
            return SCPE_FMT;                /* must be error, exit */
        wc = (int32)(bdata);                /* byte count in tape record */
        wc = (wc + 1)/2;                    /* change byte count into hw count */
        if (wc == 0)
            return SCPE_OK;                 /* eof found, return */
        /* copy data to memory in 16 bit halfwords */
        while (wc-- != 0) {
            if (get_halfword(fileref, &hdata))  /* get 16 bits of data */
                return SCPE_FMT;            /* must be error, exit */
            ((uint16*)M)[ma++] = hdata;     /* put the hw into memory */
        }
        /* look only for record byte count */
        if (get_word(fileref, &edata))      /* read 4 bytes of data */
            return SCPE_FMT;                /* must be error, exit */
        /* the before and after byte count must be equal */
        if (bdata != edata)
            return SCPE_FMT;                /* must be error, exit */
    }
    return SCPE_OK;                         /* never here */
}
#endif

/* ICL formats */
/***********************************************
 *
 * *DEVXX=FCILCASA (,N)
 *
 * *DEV     defines a controller definition entry
 * XX       hex address that will be used by I/O instructions to address controller
 * =        required delimiter for following 8 hex characters
 * F        flag used for I/O emulation by CPU, not used but must be zero.
 * C        defines the class of controller:
 *          0 = Line Printer
 *          1 = Card Reader
 *          2 = Teletype
 *          3 = Interval Timer
 *          4 = Panel
 *          5-D Unassigned
 *          E = All Others
 *          F = Extended I/O
 * IL       Controller interrupt priority level of Service Interrupt (0x14 - 0x23)
 * CA       Controller address defined by hardware switches on controller
 * SA       Lowest controller device subaddress.  Usually zero when only 1 device configured
 *              The subaddress field (SA) must reflect the following for TLC controller:
 *              00 = Card Reader
 *              01 = Teletype
 *              02 = Line Printer
 * ()       denotes optional parameter
 * ,NN      2 digit hexx number of devices configured on the controller`
 *
 ***********************************************
 *
 * *INTXX=RS
 * *INT     defines interrupt definition entry
 * XX       Hex interrupt priority level to be defined
 * =        required delimiter for following 2 hex characters
 * R        Hex RTOM board number to which the interrupt XX is assigned
 * S        1's complement of the hex subaddress of the RTOM board
 *              assigned to the interrupt XX
 *
 *          RTOM physical controller address 0x79 is RTOM board number 1, RTOM
 *              address 0x7A is board number 2, etc.
 *          Real-Time Clock is connected tp subaddress 6 on RTOM board
 *          Interval Timer is connected to subaddress 4 on RTOM board
 *          RTOM physical address must be 0x79 or above to be able to
 *              support up to seven RTOM boards for maximum configuration
 *              of 112 interrupt levels (7 x 16).
 *
 ***********************************************
 *
 * *END
 * *END     Defines the last record of the Initial Configuration Load file.
 *
 ***********************************************
*/

/* Example device entry
 * *DEV04=0E140100,04
 *      The controller is "E" class
 *      CPU command device address will be 0x04
 *      The priority of the Service Interrupt is 0x14
 *      The first device has suaddress of 00 and there are 4 devices defined
 *      There will be four devices defined in SPAD. The I/O commands (CD and TD)
 *          will address the devices as 0x04, 0x05, 0x06, and 0x07.
 *      The physical address of the controller is 0x10.
 *      Assigning SI address of 0x14 means:
 *          The transfer Interrupt location for priority 0x14 is 0x100.
 *          The Service Interrupt vector location for priority 0x14 is 0x140.
 *          The emulation IOCD will be stored at loation 0x700.
 *          The interrupt control instructions (DI, DI, RI, AI, DAI) will control
 *              the interrupt of the controller by addressing priority 0x14.
 *
 *  Example interrupt entry (RTOM)
 *  *INT28=16
 *      The interrupt control instructions (DI, EI, RI, AI, DAI) will control
 *          the interrupt on the RTOM by addressing priority 0x28.
 *      The RTOM board is 1
 *      The subaddress on the board is 0x06 (jumpered locic subaddress is 9)
 */

/*
 * Example ICL file
 * *DEV04=0E150400,02       Cartridge disc with two platters
 * *DEV08=0E160800,04       Moving head disc
 * *DEV10=0E181000,04       9-Track magnetic tape
 * *DEV20=0E1A2000,10       GPMC with 16 terminals
 * *DEV60=0E1E6000,08       ADS
 * *DEV78=01207800          Primary card reader
 * *DEV7A=00217802          Primary line printer
 * *DEV7E=02237801          Primary Teletype
 * *INT00=1F                Power fail/Auto restart
 * *INT01=1E                System Overide
 * *INT12=1D                Memory parity
 * *INT13=1C                Console Interrupt
 * *INT24=1B                Nonpresent memory
 * *INT25=1A                Undefined instruction trap
 * *INT26=19                Privlege violation
 * *INT27=18                Call Monitor
 * *INT28=16                Real-time clock
 * *INT29=17                Arithmetic exception
 * *INT2A=15                External interrupt
 * *INT2B=14                External interrupt
 * *INT2C=13                External interrupt
 * *INT2D=12                External interrupt
 * *END
 */
 
/* process two hex input characters into a number
 * pt - input char pointer
 * val - word oiunter when number will be saved
 * return SCPE_OK for OK
 * or SCPE_ARG for arg error (bad number)
 */
t_value get_2hex(char *pt, uint32 *val)
{
    int32 hexval;
    uint32 c1 = sim_toupper((uint32)pt[0]); /* first hex char */
    uint32 c2 = sim_toupper((uint32)pt[1]); /* next hex char */

    if (isdigit(c1))                        /* digit */
        hexval = c1 - (uint32)'0';          /* get value */
    else
    if (isxdigit(c1))                       /* hex digit */
        hexval = c1 - (uint32)'A' + 10;     /* get hex value */
    else
        return SCPE_ARG;                    /* oops, error */
    hexval <<= 4;                           /* move to upper nibble */
    if (isdigit(c2))                        /* digit */
        hexval += c2 - (uint32)'0';         /* get value */
    else
    if (isxdigit(c2))                       /* hex digit */
        hexval += c2 - (uint32)'A' + 10;    /* get hex value */
    else
        return SCPE_ARG;                    /* oops, error */
    *val = hexval;                          /* return value to caller */
    return SCPE_OK;                         /* all OK */
}

/* load an ICL file and configure SPAD interupt and device entries */
/* SPAD keyword will not be set and will be set when MPX or UTX is loaded */
/* return SCPE_OK on load complete */
t_stat load_icl(FILE *fileref)
{
    char        *cp;                        /* work pointer in buf[] */
    uint32      sa;                         /* spad address */
    uint32      dev;                        /* device entry */
    uint32      intr;                       /* interrupt entry */
    uint32      data;                       /* entry data */
    uint32      cls;                        /* device class */
    uint32      ivl;                        /* Interrupt Vector Location */
    uint32      i;                          /* just a tmp */
    char        buf[120];                   /* input buffer */

    /* read file input records until the end */
    while (fgets(&buf[0], 120, fileref) != 0) {
        /* skip any white spaces */
        for(cp = &buf[0]; *cp == ' ' || *cp == '\t'; cp++);
        if (*cp++ != '*')
            continue;                       /* if line does not start with *, ignore */
        if(sim_strncasecmp(cp, "END", 3) == 0) {
            return SCPE_OK;                 /* we are done */
        }
        else
        if(sim_strncasecmp(cp, "DEV", 3) == 0) {
            /* process device entry */
            /*
            |----+----+----+----+----+----+----+----|
            |Flgs|CLS |0|Int Lev|0|Phy Adr|Sub Addr |
            |----+----+----+----+----+----+----+----|
            */
            for(cp += 3; *cp == ' ' || *cp == '\t'; cp++);  /* skip white spaces */
            if (get_2hex(cp, &dev) != SCPE_OK)      /* get the device address */
                return SCPE_ARG;            /* unknown input, argument error */
            if (dev > 0x7f)                 /* devices are 0-7f (0-127) */
                return SCPE_ARG;            /* argument error */
            sa = dev + 0x00;                /* device entry spad address is dev# + 0x00 */
            cp += 2;                        /* skip the 2 processed chars */
            if (*cp++ != '=')               /* must have = sign */
                return SCPE_ARG;            /* unknown input, argument error */
            if (get_2hex(cp, &cls) != SCPE_OK)  /* get unused '0" and class */
                return SCPE_ARG;            /* unknown input, argument error */
            cp += 2;                        /* skip the 2 processed chars */
            if (get_2hex(cp, &intr) != SCPE_OK) /* get the interrupt level value */
                return SCPE_ARG;            /* unknown input, argument error */
            if (intr > 0x6f)                /* ints are 0-6f (0-111) */
                return SCPE_ARG;            /* argument error */
            /* put class and 1's intr in place */
            dev = ((~intr & 0x7f) << 16) | ((cls & 0x0f) << 24);
            cp += 2;                        /* skip the 2 processed chars */
            if (get_2hex(cp, &data) != SCPE_OK) /* get the selbus physical address */
                return SCPE_ARG;            /* unknown input, argument error */
            if (data > 0x7f)                /* address is 0-7f (0-127) */
                return SCPE_ARG;            /* argument error */
            dev |= (data & 0x7f) << 8;      /* insert the physical address */
            cp += 2;                        /* skip the 2 processed chars */
            if (get_2hex(cp, &data) != SCPE_OK) /* get the starting sub address 0-ff (255) */
                return SCPE_ARG;            /* unknown input, argument error */
            if (data > 0x7f)                /* sub address is 0-ff (0-256) */
                return SCPE_ARG;            /* argument error */
            if ((cls & 0xf) != 0xf)         /* sub addr must be zero for class F */
                dev |= (data & 0xff);       /* insert the starting sub address for non f class */
            SPAD[sa] = dev;                 /* put the first device entry into the spad */
            /* see if there is an optional device count for class 'E' I/O */
            if ((cls & 0xf) == 0xe) {
                cp += 2;                    /* skip the 2 processed chars */
                if (*cp++ == ',') {         /* must have comma if optional parameters */
                    /* check for optional sub addr cnt */
                    if (get_2hex(cp, &data) != SCPE_OK) /* get the count */
                        return SCPE_ARG;    /* unknown input, argument error */
                    if (data > 0x10)        /* sub address is max of 16 */
                        return SCPE_ARG;    /* argument error */
                    for (i=0; i<data-1; i++) {  /* 1st is already stored, redice cnt by 1 */
                        /* each of the devices will share the same interrupt */
                        SPAD[++sa] = ++dev; /* put next device entry in spad for the controller */
                    }   /* done with 'E' class device with multiple devices */
                }
            }   /* done with device entry */
            /* now create an interrupt entry for the controller */
            /*
            |----+----+----+----+----+----+----+----|
            |   Flags |0|Int Lev|      Int IVL      |
            |----+----+----+----+----+----+----+----|
            */
/* TODO call function here to create 32/7x IVL location for interrupt */
/* if (CPU_MODEL < MODEL_27) get_IVL(intr, &ivl); */
            sa = intr + 0x80;               /* interrupt entry spad address is int# + 0x80 */
            ivl = (intr << 2) + 0x100;      /* default IVL base is 0x100 for Concept machines */
            intr = (intr << 16) | ivl;      /* combine int level and ivl */
            SPAD[sa] = intr;                /* put the device interrupt entry into the spad */
        }
        else
        if(sim_strncasecmp(cp, "INT", 3) == 0) {
            /* process interrupt entry */
            /*
            |----+----+----+----+----+----+----+----|
            |   Flags |1RRR|SSSS|      Int IVL      |
            |----+----+----+----+----+----+----+----|
            */
            for(cp += 3; *cp == ' ' || *cp == '\t'; cp++);  /* skip white spaces */
            if (get_2hex(cp, &intr) != SCPE_OK) /* get the interrupt level value */
                return SCPE_ARG;            /* unknown input, argument error */
            if (intr > 0x6f)                /* ints are 0-6f (0-111) */
                return SCPE_ARG;            /* argument error */
            sa = intr + 0x80;               /* interrupt entry spad address is int# + 0x80 */
/* TODO call function here to create 32/7x IVL location for interrupt */
/* if (CPU_MODEL < MODEL_27) get_IVL(intr, &ivl); */
            ivl = (intr << 2) + 0x100;      /* default IVL base is 0x100 for Concept machines */
            cp += 2;                        /* skip the 2 processed chars */
            if (*cp++ != '=')               /* must have = sign */
                return SCPE_ARG;            /* unknown input, argument error */
            if (get_2hex(cp, &data) != SCPE_OK)
                return SCPE_ARG;            /* unknown input, argument error */
            /* first digit is 3 ls bits of RTOM addr 0x79 is 001 */
            intr = 0x00800000 | ((data & 0x70) << 16);  /* put the RTOM 3 LSBs into entry */
            /* second digit is subaddress on RTOM board for interrupt connection, ~6 = 9 */
            intr |= (data & 0xf) << 16;     /* put in 1's comp of RTOM subaddress */
            /* add in the correct IVL for 32/7x or concelt machines */
            intr |= ivl;                    /* set the IVL location */
            SPAD[sa] = intr;                /* put the interrupt entry into the spad */
        }
        else
            return SCPE_ARG;                /* unknown input, argument error */
    }
    return SCPE_OK;                         /* file done */
}


/* Load a file image into memory.  */
/* file.mem files are binary files created with the makecode utility */
#ifdef NO_TAP_FOR_NOW
/* file.tap files are TAP formatted binary files created from tape images */
#endif
/* data is raw binary memory data and is loaded starting at loc 0 */

#define FMT_NONE 0
#define FMT_MEM 1
#ifdef NO_TAP_FOR_NOW
#define FMT_TAP 2
#endif
#define FMT_ICL 3
t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    int32 fmt;

    fmt = FMT_NONE;                         /* no format */
    /* match the extension to .mem for this file */
    if (match_ext(fnam, "MEM"))
        fmt = FMT_MEM;                      /* we have binary format */
    else
#ifdef NO_TAP_FOR_NOW
    if (match_ext(fnam, "TAP"))
        fmt = FMT_TAP;                      /* we have tap tape format */
    else
#endif
    /* match the extension to .icl for this file */
    if (match_ext(fnam, "ICL"))
        fmt = FMT_ICL;                      /* we have initial configuration load (ICL) format */
    else
        return SCPE_FMT;                    /* format error */

    switch (fmt) {

    case FMT_MEM:                           /* binary memory image */
        return load_mem(fileref);

#ifdef NO_TAP_FOR_NOW
    case FMT_TAP:                           /* tape file image */
        return load_tap(fileref);
#endif

    case FMT_ICL:                           /* icl file image */
        return load_icl(fileref);

#ifdef NO_TAP_FOR_NOW
    case FMT_NONE:                          /* nothing */
#endif
    default:
        break;
    }
    return SCPE_FMT;                        /* format error */
}

/* Symbol tables */

/*
 * The SEL 32 supports the following instruction formats.
 * 
 * TYPE     Format   Normal     Base Mode
 *  A       ADR      d,[*]o,x   d,o[(b)],x  FC = extra
 *  B       BRA      [*]o,x     o[(b)],x
 *  C       IMM      d,o        d,o
 *  D       BIT      d,[*]o     d,o[(b)]
 *  E       ADR      [*]o,x     o[(b)],x  FC = extra
 *  HALFWORD
 *  F       REG      s,d        s,d         Half Word
 *  G       RG1      s          s
 *  H       HLF
 *  I       SHF      d,v        d,v
 *  K       RBT      d,b        d,b
 *  L       EXR      s          s
 *  M       IOP      n,b        n,b  
 *  N       SVC      n,b        n,b  
 */

#define TYPE_A          0
#define TYPE_B          1 
#define TYPE_C          2
#define TYPE_D          3 
#define TYPE_E          4
#define TYPE_F          5 
#define TYPE_G          6
#define TYPE_H          7 
#define TYPE_I          8 
#define TYPE_K          9 
#define TYPE_L          10 
#define TYPE_M          11 
#define TYPE_N          12 
#define H               0x10                /* halfword instruction */
/* all instruction unless specified as base/nobase only will be either */
#define B               0x20                /* base register mode only */
#define N               0x40                /* non base register mode only */
#define X               0x80                /* 32/55 or 32/75 only */

typedef struct _opcode {
       uint16       opbase;
       uint16       mask;
       uint8        type;
       const char   *name;
} t_opcode;

t_opcode  optab[] = {
    {  0x0000,  0xFFFF,   H|TYPE_H,   "HALT", },    /* Halt # * */
    {  0x0001,  0xFFFF,   H|TYPE_H,   "WAIT", },    /* Wait # * */
    {  0x0002,  0xFFFF,   H|TYPE_H,   "NOP", },     /* Nop # */
    {  0x0003,  0xFC0F,   H|TYPE_G,   "LCS", },     /* Load Control Switches */
    {  0x0004,  0xFC0F,   H|TYPE_G,   "ES", },      /* Extend Sign # */
    {  0x0005,  0xFC0F,   H|TYPE_G,   "RND", },     /* Round Register # */
    {  0x0006,  0xFFFF,   H|TYPE_H,   "BEI", },     /* Block External Interrupts # */
    {  0x0007,  0xFFFF,   H|TYPE_H,   "UEI", },     /* Unblock External Interrupts # */
    {  0x0008,  0xFFFF,   H|TYPE_H,   "EAE", },     /* Enable Arithmetic Exception Trap # */
    {  0x0009,  0xFC0F,   H|TYPE_G,   "RDSTS", },   /* Read CPU Status Word * */
    {  0x000A,  0xFFFF,   H|TYPE_H,   "SIPU", },    /* Signal IPU # */
    {  0x000B,  0xFC0F,   H|TYPE_F,   "RWCS", },    /* Read Writable Control Store # */
    {  0x000C,  0xFC0F,   H|TYPE_F,   "WWCS", },    /* Write Writable Control Store # */
    {  0x000D,  0xFFFF, N|H|TYPE_H,   "SEA", },     /* Set Extended Addressing # NBR Only */
    {  0x000E,  0xFFFF,   H|TYPE_H,   "DAE", },     /* Disable Arithmetic Exception Trap # */
    {  0x000F,  0xFFFF, N|H|TYPE_H,   "CEA", },     /* Clear Extended Addressing # NBR Only */
    {  0x0400,  0xFC0F,   H|TYPE_F,   "ANR", },     /* And Register # */
    {  0x0407,  0xFC0F,   H|TYPE_G,   "SMC", },     /* Shared Memory Control # */
    {  0x040A,  0xFC0F,   H|TYPE_G,   "CMC", },     /* Cache Memory Control # */
    {  0x040B,  0xFC0F,   H|TYPE_G,   "RPSWT", },   /* Read Processor Status Word Two # */
    {  0x0800,  0xFC0F,   H|TYPE_F,   "ORR", },     /* Or Register # */
    {  0x0808,  0xFC0F,   H|TYPE_F,   "ORRM", },    /* Or Register Masked # */
    {  0x0C00,  0xFC0F,   H|TYPE_F,   "EOR", },     /* Exclusive Or Register # */
    {  0x0C08,  0xFC0F,   H|TYPE_F,   "EORM", },    /* Exclusive Or Register Masked # */
    {  0x1000,  0xFC0F,   H|TYPE_F,   "CAR", },     /* Compare Arithmetic Register # */
    {  0x1008,  0xFC0F, B|H|TYPE_F,   "SACZ", },    /* Shift and Count Zeros # BR */
    {  0x1400,  0xFC0F,   H|TYPE_F,   "CMR", },     /* Compare masked with register */
    {  0x1800,  0xFC0C,   H|TYPE_K,   "SBR", },     /* Set Bit in Register # */
    {  0x1804,  0xFC0C, B|H|TYPE_K,   "ZBR", },     /* Zero Bit In register # BR */
    {  0x1808,  0xFC0C, B|H|TYPE_K,   "ABR", },     /* Add Bit In Register # BR */
    {  0x180C,  0xFC0C, B|H|TYPE_K,   "TBR", },     /* Test Bit in Register # BR */
    {  0x1C00,  0xFC0C, N|H|TYPE_K,   "ZBR", },     /* Zero Bit in Register # NBR */ /* CON SRABR */
    {  0x1C00,  0xFC60, B|H|TYPE_I,   "SRABR", },   /* Shift Right Arithmetic # BR */ /* CON ZBM */
    {  0x1C20,  0xFC60, B|H|TYPE_I,   "SRLBR", },   /* Shift Right Logical # BR */
    {  0x1C40,  0xFC60, B|H|TYPE_I,   "SLABR", },   /* Shift Left Arithmetic # BR */
    {  0x1C60,  0xFC60, B|H|TYPE_I,   "SLLBR", },   /* Shift Left Logical # BR */
    {  0x2000,  0xFC0C, N|H|TYPE_K,   "ABR", },     /* Add Bit in Register # NBR */ /* CON SRADBR */
    {  0x2000,  0xFC60, B|H|TYPE_I,   "SRADBR", },  /* Shift Right Arithmetic Double # BR */ /* CON ABR */
    {  0x2020,  0xFC60, B|H|TYPE_I,   "SRLDBR", },  /* Shift Left Logical Double # BR */
    {  0x2040,  0xFC60, B|H|TYPE_I,   "SLADBR", },  /* Shift Right Arithmetic Double # BR */
    {  0x2060,  0xFC60, B|H|TYPE_I,   "SLLDBR", },  /* Shift Left Logical Double # BR */
    {  0x2400,  0xFC0C, N|H|TYPE_K,   "TBR", },     /* Test Bit in Register # NBR */ /* CON SRCBR */
    {  0x2400,  0xFC60, B|H|TYPE_I,   "SRCBR", },   /* Shift Right Circular # BR */ /* CON TBR */
    {  0x2440,  0xFC60, B|H|TYPE_F,   "SLCBR", },   /* Shift Left Circular # BR */
    {  0x2800,  0xFC0F,   H|TYPE_G,   "TRSW", },    /* Transfer GPR to PSD */
    {  0x2802,  0xFC0F, B|H|TYPE_F,   "XCBR", },    /* Exchange Base Registers # BR Only */
    {  0x2804,  0xFC0F, B|H|TYPE_G,   "TCCR", },    /* Transfer CC to GPR # BR Only */
    {  0x2805,  0xFC0F, B|H|TYPE_G,   "TRCC", },    /* Transfer GPR to CC # BR */
    {  0x2808,  0xFF8F, B|H|TYPE_F,   "BSUB", },    /* Branch Subroutine # BR Only */
    {  0x2808,  0xFC0F, B|H|TYPE_F,   "CALL", },    /* Procedure Call # BR Only */
    {  0x280C,  0xFC0F, B|H|TYPE_G,   "TPCBR", },   /* Transfer Program Counter to Base # BR Only */
    {  0x280E,  0xFC7F, B|H|TYPE_G,   "RETURN", },  /* Procedure Return # BR Only */
    {  0x2C00,  0xFC0F,   H|TYPE_F,   "TRR", },     /* Transfer Register to Register # */
    {  0x2C01,  0xFC0F, B|H|TYPE_F,   "TRBR", },    /* Transfer GPR to BR # */
    {  0x2C02,  0xFC0F, B|H|TYPE_F,   "TBRR", },    /* Transfer BR to GPR # BR Only */
    {  0x2C03,  0xFC0F,   H|TYPE_F,   "TRC", },     /* Transfer Register Complement # */
    {  0x2C04,  0xFC0F,   H|TYPE_F,   "TRN", },     /* Transfer Register Negative # */
    {  0x2C05,  0xFC0F,   H|TYPE_F,   "XCR", },     /* Exchange Registers # */
    {  0x2C07,  0xFC0F,   H|TYPE_G,   "LMAP", },    /* Load MAP * */
    {  0x2C08,  0xFC0F,   H|TYPE_F,   "TRRM", },    /* Transfer Register to Register Masked # */
    {  0x2C09,  0xFC0F,   H|TYPE_G,   "SETCPU", },  /* Set CPU Mode # * */
    {  0x2C0A,  0xFC0F,   H|TYPE_F,   "TMAPR", },   /* Transfer MAP to Register # * */
    {  0x2C0B,  0xFC0F,   H|TYPE_F,   "TRCM", },    /* Transfer Register Complement Masked # */
    {  0x2C0C,  0xFC0F,   H|TYPE_F,   "TRNM", },    /* Transfer Register Negative Masked # */
    {  0x2C0D,  0xFC0F,   H|TYPE_F,   "XCRM", },    /* Exchange Registers Masked # */
    {  0x2C0E,  0xFC0F,   H|TYPE_F,   "TRSC", },    /* Transfer Register to Scratchpad # * */
    {  0x2C0F,  0xFC0F,   H|TYPE_F,   "TSCR", },    /* Transfer Scratchpad to Register # * */
    {  0x3000,  0xFC0F, X|H|TYPE_F,   "CALM", },    /* Call Monitor 32/55 # */
    {  0x3400,  0xFC00,   N|TYPE_D,   "LA", },      /* Load Address NBR Note! FW instruction */
    {  0x3800,  0xFC0F,   H|TYPE_F,   "ADR", },     /* Add Register to Register # */
    {  0x3801,  0xFC0F,   H|TYPE_F,   "ADRFW", },   /* Add Floating Point to Register # */
    {  0x3802,  0xFC0F, B|H|TYPE_F,   "MPR", },     /* Multiply Register BR # */
    {  0x3803,  0xFC0F,   H|TYPE_F,   "SURFW", },   /* Subtract Floating Point Register # */
    {  0x3804,  0xFC0F,   H|TYPE_F,   "DVRFW", },   /* Divide Floating Point Register # */
    {  0x3805,  0xFC0F,   H|TYPE_F,   "FIXW", },    /* Fix Floating Point Register # */
    {  0x3806,  0xFC0F,   H|TYPE_F,   "MPRFW", },   /* Multiply Floating Point Register # */
    {  0x3807,  0xFC0F,   H|TYPE_F,   "FLTW", },    /* Float Floating Point Register # */
    {  0x3808,  0xFC0F,   H|TYPE_F,   "ADRM", },    /* Add Register to Register Masked # */
    {  0x3809,  0xFC0F,   H|TYPE_F,   "ADRFD", },   /* Add Floating Point Register to Register # */
    {  0x380A,  0xFC0F, B|H|TYPE_F,   "DVR", },     /* Divide Register by Registier BR # */
    {  0x380B,  0xFC0F,   H|TYPE_F,   "SURFD", },   /* Subtract Floating Point Double # */
    {  0x380C,  0xFC0F,   H|TYPE_F,   "DVRFD", },   /* Divide Floating Point Double # */
    {  0x380D,  0xFC0F,   H|TYPE_F,   "FIXD", },    /* Fix Double Register # */
    {  0x380E,  0xFC0F,   H|TYPE_F,   "MPRFD", },   /* Multiply Double Register # */
    {  0x380F,  0xFC0F,   H|TYPE_F,   "FLTD", },    /* Float Double # */
    {  0x3C00,  0xFC0F,   H|TYPE_F,   "SUR", },     /* Subtract Register to Register # */
    {  0x3C08,  0xFC0F,   H|TYPE_F,   "SURM", },    /* Subtract Register to Register Masked # */
    {  0x4000,  0xFC0F, N|H|TYPE_F,   "MPR", },     /* Multiply Register to Register # NBR */
    {  0x4400,  0xFC0F, N|H|TYPE_F,   "DVR", },     /* Divide Register to Register # NBR */
    {  0x5000,  0xFC08,   B|TYPE_D,   "LABRM", },   /* Load Address BR Mode */
    {  0x5400,  0xFC08,   B|TYPE_A,   "STWBR", },   /* Store Base Register BR Only */
    {  0x5800,  0xFC08,   B|TYPE_A,   "SUABR", },   /* Subtract Base Register BR Only */
    {  0x5808,  0xFC08,   B|TYPE_D,   "LABR", },    /* Load Address Base Register BR Only */
    {  0x5C00,  0xFC08,   B|TYPE_A,   "LWBR", },    /* Load Base Register BR Only */
    {  0x5C08,  0xFF88,   B|TYPE_B,   "BSUBM", },   /* Branch Subroutine Memory BR Only */
    {  0x5C08,  0xFC08,   B|TYPE_B,   "CALLM", },   /* Call Memory BR Only */
    {  0x6000,  0xFC0F, N|H|TYPE_F,   "NOR", },     /* Normalize # NBR Only */
    {  0x6400,  0xFC0F, N|H|TYPE_F,   "NORD", },    /* Normalize Double #  NBR Only */
    {  0x6800,  0xFC0F, N|H|TYPE_F,   "SCZ", },     /* Shift and Count Zeros # */
    {  0x6C00,  0xFC40, N|H|TYPE_I,   "SRA", },     /* Shift Right Arithmetic # NBR */
    {  0x6C40,  0xFC40, N|H|TYPE_I,   "SLA", },     /* Shift Left Arithmetic # NBR */
    {  0x7000,  0xFC40, N|H|TYPE_I,   "SRL", },     /* Shift Right Logical # NBR */
    {  0x7040,  0xFC40, N|H|TYPE_I,   "SLL", },     /* Shift Left Logical # NBR */
    {  0x7400,  0xFC40, N|H|TYPE_I,   "SRC", },     /* Shift Right Circular # NBR */
    {  0x7440,  0xFC40, N|H|TYPE_I,   "SLC", },     /* Shift Left Circular # NBR */
    {  0x7800,  0xFC40, N|H|TYPE_I,   "SRAD", },    /* Shift Right Arithmetic Double # NBR */
    {  0x7840,  0xFC40, N|H|TYPE_I,   "SLAD", },    /* Shift Left Arithmetic Double # NBR */
    {  0x7C00,  0xFC40, N|H|TYPE_I,   "SRLD", },    /* Shift Right Logical Double # NBR */
    {  0x7C40,  0xFC40, N|H|TYPE_I,   "SLLD", },    /* Shift Left Logical Double # NBR */
    {  0x8000,  0xFC08,   TYPE_A,     "LEAR", },    /* Load Effective Address Real * */
    {  0x8400,  0xFC00,   TYPE_A,     "ANM", },     /* And Memory B,H,W,D */
    {  0x8800,  0xFC00,   TYPE_A,     "ORM", },     /* Or Memory B,H,W,D */
    {  0x8C00,  0xFC00,   TYPE_A,     "EOM", },     /* Exclusive Or Memory */
    {  0x9000,  0xFC00,   TYPE_A,     "CAM", },     /* Compare Arithmetic with Memory */
    {  0x9400,  0xFC00,   TYPE_A,     "CMM", },     /* Compare Masked with Memory */
    {  0x9800,  0xFC00,   TYPE_D,     "SBM", },     /* Set Bit in Memory */
    {  0x9C00,  0xFC00,   TYPE_D,     "ZBM", },     /* Zero Bit in Memory */
    {  0xA000,  0xFC00,   TYPE_D,     "ABM", },     /* Add Bit in Memory */
    {  0xA400,  0xFC00,   TYPE_D,     "TBM", },     /* Test Bit in Memory */
    {  0xA800,  0xFC00,   TYPE_B,     "EXM", },     /* Execute Memory */
    {  0xAC00,  0xFC00,   TYPE_A,     "L", },       /* Load B,H,W,D */
    {  0xB000,  0xFC00,   TYPE_A,     "LM", },      /* Load Masked B,H,W,D */
    {  0xB400,  0xFC00,   TYPE_A,     "LN", },      /* Load Negative B,H,W,D */
    {  0xB800,  0xFC00,   TYPE_A,     "ADM", },     /* Add Memory B,H,W,D */
    {  0xBC00,  0xFC00,   TYPE_A,     "SUM", },     /* Subtract Memory B,H,W,D */
    {  0xC000,  0xFC00,   TYPE_A,     "MPM", },     /* Multiply Memory B,H,W,D */
    {  0xC400,  0xFC00,   TYPE_A,     "DVM", },     /* Divide Memory B,H,W,D */
    {  0xC800,  0xFC0F,   TYPE_C,     "LI", },      /* Load Immediate */
    {  0xC801,  0xFC0F,   TYPE_C,     "ADI", },     /* Add Immediate */
    {  0xC802,  0xFC0F,   TYPE_C,     "SUI", },     /* Subtract Immediate */
    {  0xC803,  0xFC0F,   TYPE_C,     "MPI", },     /* Multiply Immediate */
    {  0xC804,  0xFC0F,   TYPE_C,     "DVI", },     /* Divide Immediate */
    {  0xC805,  0xFC0F,   TYPE_C,     "CI", },      /* Compare Immediate */
    {  0xC806,  0xFC0F,   TYPE_N,     "SVC", },     /* Supervisor Call */
    {  0xC807,  0xFC0F,   TYPE_G,     "EXR", },     /* Execute Register/ Right */
    {  0xC808,  0xFC0F, X|TYPE_A,     "SEM", },     /* Store External Map 32/7X * */
    {  0xC809,  0xFC0F, X|TYPE_A,     "LEM", },     /* Load External Map 32/7X * */
    {  0xC80A,  0xFC0F, X|TYPE_A,     "CEMA", },    /* Convert External Map 32/7X * */
    {  0xCC00,  0xFC08,   TYPE_A,     "LF", },      /* Load File */
    {  0xCC08,  0xFC08,   TYPE_A,     "LFBR", },    /* Load Base File */
    {  0xD000,  0xFC00, N|TYPE_A,     "LEA", },     /* Load Effective Address # NBR */
    {  0xD400,  0xFC00,   TYPE_A,     "ST", },      /* Store B,H,W,D */
    {  0xD800,  0xFC00,   TYPE_A,     "STM", },     /* Store Masked B,H,W,D */
    {  0xDC00,  0xFC08,   TYPE_A,     "STF", },     /* Store File */
    {  0xDC08,  0xFC08,   TYPE_A,     "STFBR", },   /* Store Base File */
    {  0xE000,  0xFC08,   TYPE_A,     "SUF", },     /* Subtract Floating Memory D,W */
    {  0xE008,  0xFC08,   TYPE_A,     "ADF", },     /* Add Floating Memory D,W */
    {  0xE400,  0xFC08,   TYPE_A,     "DVF", },     /* Divide Floating Memory D,W */
    {  0xE408,  0xFC08,   TYPE_A,     "MPF", },     /* Multiply Floating Memory D,W */
    {  0xE800,  0xFC00,   TYPE_A,     "ARM", },     /* Add Register to Memory B,H,W,D */
    {  0xEC00,  0xFF80,   TYPE_B,     "BU", },      /* Branch Unconditional */
    {  0xEC00,  0xFF80,   TYPE_A,     "BCT", },     /* Branch Condition True */
    {  0xEC80,  0xFF80,   TYPE_B,     "BS", },      /* Branch Condition True CC1 = 1 */
    {  0xED00,  0xFF80,   TYPE_B,     "BGT", },     /* Branch Condition True CC2 = 1 */
    {  0xED80,  0xFF80,   TYPE_B,     "BLT", },     /* Branch Condition True CC3 = 1 */
    {  0xEE00,  0xFF80,   TYPE_B,     "BEQ", },     /* Branch Condition True CC4 = 1 */
    {  0xEE80,  0xFF80,   TYPE_B,     "BGE", },     /* Branch Condition True CC2|CC4 = 1 */
    {  0xEF00,  0xFF80,   TYPE_B,     "BLE", },     /* Branch Condition True CC3|CC4 = 1 */
    {  0xEF80,  0xFF80,   TYPE_B,     "BANY", },    /* Branch Condition True CC1|CC2|CC3|CC4 */
    {  0xF000,  0XFF80,   TYPE_B,     "BFT", },     /* Branch Function True */
    {  0xF000,  0xFF80,   TYPE_A,     "BCF", },     /* Branch Condition False */
    {  0xF080,  0xFF80,   TYPE_B,     "BNS", },     /* Branch Condition False CC1 = 0 */
    {  0xF100,  0xFF80,   TYPE_B,     "BNP", },     /* Branch Condition False CC2 = 0 */
    {  0xF180,  0xFF80,   TYPE_B,     "BNN", },     /* Branch Condition False CC3 = 0 */
    {  0xF200,  0xFF80,   TYPE_B,     "BNE", },     /* Branch Condition False CC4 = 0 */
    {  0xF280,  0xFF80,   TYPE_B,     "BCF 5,", },  /* Branch Condition False CC2|CC4 = 0 */
    {  0xF300,  0xFF80,   TYPE_B,     "BCF 6,", },  /* Branch Condition False CC3|CC4 = 0 */
    {  0xF380,  0xFF80,   TYPE_B,     "BAZ", },     /* Branch Condition False CC1|CC2|CC3|CC4=0*/
    {  0xF400,  0xFC70,   TYPE_D,     "BIB", },     /* Branch after Incrementing Byte */
    {  0xF420,  0xFC70,   TYPE_D,     "BIH", },     /* Branch after Incrementing Half */
    {  0xF440,  0xFC70,   TYPE_D,     "BIW", },     /* Branch after Incrementing Word */
    {  0xF460,  0xFC70,   TYPE_D,     "BID", },     /* Branch after Incrementing Double */
    {  0xF800,  0xFF80,   TYPE_E,     "ZM", },      /* Zero Memory B,H,W,D */
    {  0xF880,  0xFF80,   TYPE_B,     "BL", },      /* Branch and Link */
    {  0xF900,  0xFCC0, X|TYPE_B,     "BRI", },     /* Branch and Reset Interrupt 32/55 * */
    {  0xF980,  0xFF80,   TYPE_B,     "LPSD", },    /* Load Program Status Double * */
    {  0xFA08,  0xFC00,   TYPE_B,     "JWCS", },    /* Jump to Writable Control Store * */
    {  0xFA80,  0xFF80,   TYPE_B,     "LPSDCM", },  /* LPSD and Change Map * */
    {  0xFB00,  0xFCC0, X|TYPE_A,     "TRP", },     /* Transfer Register to Protect Register 32/7X */
    {  0xFB80,  0xFCC0, X|TYPE_A,     "TPR", },     /* Transfer Protect Register to Register 32/7X */
    {  0xFC00,  0xFC07,   TYPE_L,     "EI", },      /* Enable Interrupt */
    {  0xFC01,  0xFC07,   TYPE_L,     "DI", },      /* Disable Interrupt */
    {  0xFC02,  0xFC07,   TYPE_L,     "RI", },      /* Request Interrupt */
    {  0xFC03,  0xFC07,   TYPE_L,     "AI", },      /* Activate Interrupt */
    {  0xFC04,  0xFC07,   TYPE_L,     "DAI", },     /* Deactivate Interrupt */
    {  0xFC05,  0xFC07,   TYPE_M,     "TD", },      /* Test Device */
    {  0xFC06,  0xFC07,   TYPE_M,     "CD", },      /* Command Device */
    {  0xFC17,  0xFC7F,   TYPE_C,     "SIO", },     /* Start I/O */
    {  0xFC1F,  0xFC7F,   TYPE_C,     "TIO", },     /* Test I/O */
    {  0xFC27,  0xFC7F,   TYPE_C,     "STPIO", },   /* Stop I/O */
    {  0xFC2F,  0xFC7F,   TYPE_C,     "RSCHNL", },  /* Reset Channel */
    {  0xFC37,  0xFC7F,   TYPE_C,     "HIO", },     /* Halt I/O */
    {  0xFC3F,  0xFC7F,   TYPE_C,     "GRIO", },    /* Grab Controller */
    {  0xFC47,  0xFC7F,   TYPE_C,     "RSCTL", },   /* Reset Controller */
    {  0xFC4F,  0xFC7F,   TYPE_C,     "ECWCS", },   /* Enable Channel WCS Load */
    {  0xFC5F,  0xFC7F,   TYPE_C,     "WCWCS", },   /* Write Channel WCS */
    {  0xFC67,  0xFC7F,   TYPE_C,     "ECI", },     /* Enable Channel Interrupt */
    {  0xFC6F,  0xFC7F,   TYPE_C,     "DCI", },     /* Disable Channel Interrupt */
    {  0xFC77,  0xFC7F,   TYPE_C,     "ACI", },     /* Activate Channel Interrupt */
    {  0xFC7F,  0xFC7F,   TYPE_C,     "DACI", },    /* Deactivate Channel Interrupt */
};

/* Instruction decode printing routine
   Inputs:
    *of       =       output stream
    val       =       16/32 bit instruction to print left justified
    sw        =       mode switches, 'M'=base mode, 'N'=nonbase mode
*/
const char *fc_type = "WHDHBBBB";   /* F & C bit values */

int fprint_inst(FILE *of, uint32 val, int32 sw)
{
    uint16   inst = (val >> 16) & 0xFFFF;
    int      i;
    int      mode = 0;                      /* assume non base mode instructions */
    t_opcode *tab;

    if ((PSD[0] & 0x02000000) || (sw & SWMASK('M'))) /* bit 6 is base mode */
        mode = 1;
    /* loop through the instruction table for an opcode match and get the type */ 
    for (tab = optab; tab->name != NULL; tab++) {
        if (tab->opbase == (inst & tab->mask)) {
            if (mode && (tab->type & (X | N)))
                continue;                   /* non basemode instruction in base mode, skip */
            if (!mode && (tab->type & B))
                continue;                   /* basemode instruction in nonbase mode, skip */

            /* TODO?  Maybe want to make sure MODEL is 32/7X for X type instructions */

            /* match found */
            fputs(tab->name, of);           /* output the base opcode */

            /* process the other fields of the instruction */
            switch(tab->type & 0xF) {
            /* memory reference instruction */
            case TYPE_A:                    /* r,[*]o[,x] or r,o[(b)][,x] */
            /* zero memory instruction */
            case TYPE_E:                    /* [*]o[,x] or o[(b)][,x] */
                /* append B, H, W, D to base instruction using F & C bits */
                i = (val & 3) | ((inst >> 1) & 04);
                if (((inst&0xfc00) == 0xe000) ||
                    ((inst&0xfc00) == 0xe400))
                    i &= ~4;                /* remove f bit from fpt instr */
                if (((inst&0xfc00) != 0xdc00) &&
                    ((inst&0xfc00) != 0xd000) &&
                    ((inst&0xfc00) != 0x5400) &&
                    ((inst&0xfc00) != 0x5800) &&
                    ((inst&0xfc00) != 0x5c00) &&
                    ((inst&0xfc00) != 0xcc00) &&
                    ((inst&0xfc00) != 0x8000))
                    fputc(fc_type[i], of);
                /* Fall through */

            /* BIx instructions or bit in memory reference instructions */
            case TYPE_D:                    /* r,[*]o[,x] or r,o[(b)],[,x] */
                if ((tab->type & 0xF) != TYPE_E) {
                    fputc(' ', of);
//                  fputc('R', of);
                    /* output the reg or bit number */
                    fputc('0'+((inst>>7) & 07), of);
                    fputc(',', of);
                }
                /* Fall through */

            /* branch instruction */
            case TYPE_B:                    /* [*]o[,x] or o[(b)],[,x] */
                if (((tab->type & 0xf) != TYPE_A) && ((tab->type & 0xf) != TYPE_D))
                    fputc(' ', of);
                if (mode) {
                    /* base reg mode */
                    fprint_val(of, val&0xffff, 16, 16, PV_LEFT);    /* output 16 bit offset */
                    if (inst & 07) {
                        fputc('(', of);
//                      fputc('B', of);
                        fputc(('0'+(inst & 07)), of);   /* output the base reg number */
                        fputc(')', of);
                    }
                    if (inst & 0x70) {
                        fputc(',', of);
//                      fputc('R', of);
                        fputc(('0'+((inst >> 4) & 07)), of);    /* output the index reg number */
                    }
                } else {
                    /* nonbase reg mode */
                    if (inst & 0x10)
                        fputc('*', of);     /* show indirection */
                    fprint_val(of, val&0x7ffff, 16, 19, PV_LEFT);   /* 19 bit offset */
                    if (inst & 0x60) {
                        fputc(',', of);     /* register coming */
//                      fputc('R', of);
                        if (tab->type != TYPE_D)
                            fputc('0'+((inst & 0x60) >> 5), of);    /* output the index reg number */
                        else { 
                            if ((inst & 0xfc00) != 0xf400)
                                fputc('0'+((inst & 0x60) >> 5), of);    /* output the index reg number */
                        }
                    }
                }
                break;

            /* immediate or XIO instructions */
            case TYPE_C:                    /* r,v */
                fputc(' ', of);
//              fputc('R', of);
                fputc('0'+((inst>>7) & 07), of);    /* index reg number */
                fputc(',', of);
                fprint_val(of, val&0xffff, 16, 16, PV_LEFT);    /* 16 bit imm val or chan/suba */
                break;

            /* reg - reg instructions */
            case TYPE_F:                    /* rs,rd */
                fputc(' ', of);
//              fputc('R', of);
                fputc('0'+((inst>>4) & 07), of);    /* src reg */
                fputc(',', of);
//              fputc('R', of);
                fputc('0'+((inst>>7) & 07), of);    /* dest reg */
                break;

            /* single reg instructions */
            case TYPE_G:                    /* op r */
                fputc(' ', of);
//              fputc('R', of);
                fputc('0'+((inst>>7) & 07), of);    /* output src/dest reg  num */
                break;

            /* just output the instruction */
            case TYPE_H:                    /* empty */
                break;

            /* reg and bit shift cnt */
            case TYPE_I:                    /* r,b */
                fputc(' ', of);
//              fputc('R', of);
                fputc('0'+((inst>>7) & 07), of);    /* reg number */
                fputc(',', of);
                fprint_val(of, inst&0x1f, 10, 5, PV_LEFT);  /* 5 bit shift count */
                break; 

            /* register bit operations */
            case TYPE_K:                    /* r,rb */
                fputc(' ', of);
//              fputc('R', of);
                fputc('0'+((inst>>4) & 07), of);    /* register number */
                fputc(',', of);
                i = ((inst & 3) << 3) | ((inst >> 7) & 07);
                fprint_val(of, i, 10, 5, PV_LEFT);  /* reg bit number to operate on */
                break; 

            /* interrupt control instructions */
            case TYPE_L:                    /* i */
                fputc(' ', of);
                fprint_val(of, (inst>>3)&0x7f, 16, 7, PV_RZRO); /* output 7 bit priority level value */
                break;

            /* CD/TD Class E I/O instructions */
            case TYPE_M:                    /* i,v */
                fputc(' ', of);
                fprint_val(of, (inst>>3)&0x7f, 16, 7, PV_RZRO); /* output 7 bit device address */
                fputc(',', of);
                fprint_val(of, (val&0xffff), 16, 16, PV_RZRO);  /* output 16 bit command code */
                break;

            /* SVC  instructions */
            case TYPE_N:                    /* i,v */
                fputc(' ', of);
                fprint_val(of, (val>>12)&0xf, 16, 4, PV_RZRO);  /* output 4 bit svc number */
                fputc(',', of);
                fprint_val(of, (val & 0xFFF), 16, 12, PV_LEFT); /* output 12 bit command code */
                break;

            default:
                /* FIXME - return error code here? */
//              /* fputs(" unknown type", of);  /* output error message */
//              return SCPE_ARG;            /* unknown type */
                break;
            }
            /* return the size of the instruction */
            return (tab->type & H) ? 2 : 4;
        }
    }
    /* FIXME - should we just return error here? or dump as hex data? */
    /* we get here if opcode not found, print data value */
    if (mode)
        fputs(" Binvld ", of);              /* output basemode error message */
    else
        fputs(" Ninvld ", of);              /* output non-basmode error message */
    fprint_val(of, val, 16, 32, PV_RZRO);   /* output unknown 32 bit instruction code */
    return 4;                               /* show as full word size */
}

/* Symbolic decode

   Inputs:
       *of      =       output stream
       addr     =       current PC
       *val     =       pointer to values
       *uptr    =       pointer to unit
       sw       =       switches
   Outputs:
       return   =       status code
*/
t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
    int         i;
    int         l = 4;                      /* default to full words */
    int         rdx = 16;                   /* default radex is hex */
    uint32      num;
//  uint32      tmp=*val;                   /* for debug */

    if (sw & SIM_SW_STOP) {                 /* special processing for step */
        if (PSD[0] & 0x02000000) {          /* bit 6 is base mode */
            sw |= SWMASK('M');              /* display basemode */
            sw &= ~SWMASK('N');             /* no non-based display */
        } else {
            sw |= SWMASK('N');              /* display non-basemode */
            sw &= ~SWMASK('M');             /* no basemode display */
        }
    }
    if (addr & 0x02)
        l = 2;
    /* determine base for number output */
    if (sw & SWMASK ('D')) 
        rdx = 10;                           /* decimal */
    else
    if (sw & SWMASK ('O')) 
        rdx = 8;                            /* octal */
    else
    if (sw & SWMASK ('H')) 
        rdx = 16;                           /* hex */

    if (sw & SWMASK ('M')) {                /* machine base mode? */
        sw &= ~ SWMASK('B');                /* Can't do B and M at same time */
        sw &= ~ SWMASK('C');                /* Can't do C and M at same time */
        if (addr & 0x02)
            l = 2;
        else
            l = 4;
    } else
    if (sw & SWMASK('F')) {
        l = 4;                              /* words are 4 bytes */
    } else
    if (sw & SWMASK('W')) {
        l = 2;                              /* halfwords are 2 bytes */
    } else
    if (sw & SWMASK('B')) { 
        l = 1;                              /* bytes */
    }

    if (sw & SWMASK ('C')) {
        fputc('\'', of);                    /* opening apostorphe */
        for(i = 0; i < l; i++) {
            int ch = val[i] & 0xff;         /* get the char */
            if (ch >= 0x20 && ch <= 0x7f)   /* see if printable */
                fprintf(of, "%c", ch);      /* output the ascii char */
            else
                fputc('_', of);             /* use underscore for unprintable char */
        }
        fputc('\'', of);                    /* closing apostorphe */
    } else
    /* go print the symbolic instruction for base or nonbase mode */
    if (sw & (SWMASK('M') | SWMASK('N'))) { 
        num = 0;
        for (i = 0; i < l && i < 4; i++) {
            num |= (uint32)val[i] << ((l-i-1) * 8); /* collect 8-32 bit data value to print */
        }
        if (addr & 0x02)
            num <<= 16;                     /* use rt hw */
        l = fprint_inst(of, num, sw);       /* go print the instruction */
        if (((addr & 2) == 0) && (l == 2)) {    /* did we execute a left halfword instruction */
            fprintf(of, "; ");
            l = fprint_inst(of, num<<16, sw);   /* go print right halfword instruction */
            l = 4;                          /* next word address */
        }
    } else {
        /* print the numeric value of the memory data */
        num = 0;
        for (i = 0; i < l && i < 4; i++) 
            num |= (uint32)val[i] << ((l-i-1) * 8); /* collect 8-32 bit data value to print */
        fprint_val(of, num, rdx, l*8, PV_RZRO); /* print it in requested radix */
    }
    return -(l-1);                          /* will be negative if we did anything */
}

/* 
 * Collect offset in radix.
 */
t_stat get_off (CONST char *cptr, CONST char **tptr, uint32 radix, t_value *val, char *m)
{
    t_stat r = SCPE_OK;                     /* assume OK return */

    *m = 0;                                 /* left parend found flag if set */
    *val = (uint32)strtotv(cptr, tptr, radix);  /* convert to value */
    if (cptr == *tptr)
        r = SCPE_ARG;                       /* no argument found error */
    else {
        cptr = *tptr;                       /* where to start looking */
        while (sim_isspace(*cptr))
            cptr++;                         /* skip any spaces */
        if (*cptr++ == '(') {
           *m = 1;                          /* show we found a left parend */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any spaces */
        }
        *tptr = cptr;                       /* return next char pointer */
    }
    return r;                               /* return status */
}

/* 
 * Collect immediate in radix.
 */
t_stat get_imm (CONST char *cptr, CONST char **tptr, uint32 radix, t_value *val)
{
    t_stat r;

    r = SCPE_OK;
    *val = (uint32)strtotv (cptr, tptr, radix);
    if ((cptr == *tptr) || (*val > 0xffff))
        r = SCPE_ARG;
    else {
        cptr = *tptr;
       while (sim_isspace (*cptr)) cptr++;
     *tptr = cptr;
    }
    return r;
}

/* Symbolic input
   Inputs:
       *cptr      =       pointer to input string
       addr       =       current PC
       uptr       =       pointer to unit
       *val       =       pointer to output values
       sw         =       switches
   Outputs:
       status     =       error status
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
    int        i;
    int        x;
    int        l = 4;                       /* default to full words */
    int        rdx = 16;                    /* default radex is hex */
    char       mod = 0;
    t_opcode   *tab;
    t_stat     r;
    uint32     num;
    uint32     max[5] = {0, 0xff, 0xffff, 0, 0xffffffff};
    CONST char *tptr;
    char       gbuf[CBUFSIZE];

    /* determine base for numbers */
    if (sw & SWMASK ('D')) 
        rdx = 10;                           /* decimal */
    else
    if (sw & SWMASK ('O')) 
        rdx = 8;                            /* octal */
    else
    if (sw & SWMASK ('H')) 
        rdx = 16;                           /* hex */

    /* set instruction size */
    if (sw & SWMASK('F')) {
        l = 4;
    } else
    if (sw & SWMASK('W')) {
        l = 2;
    }

    /* process a character string */
    if (sw & SWMASK ('C')) {
        cptr = get_glyph_quoted(cptr, gbuf, 0); /* Get string */
        for(i = 0; gbuf[i] != 0; i++) {
            val[i] = gbuf[i];               /* copy in the string */
        }
        return -(i - 1);
    }

    /* see if we are processing a nonbase instruction */
    if (sw & SWMASK ('N')) {
        /* process nonbased instruction */
        cptr = get_glyph(cptr, gbuf, 0);    /* Get uppercase opcode */
        l = strlen(gbuf);                   /* opcode length */
        /* try to find the opcode in the table */
        for (tab = optab; tab->name != NULL; tab++) {
            i = tab->type & 0xf;            /* get the instruction type */
            /* check for memory reference instruction */
            if (i == TYPE_A || i == TYPE_E) {
                /* test for base opcode name without B, H, W, D applied */
                if (sim_strncasecmp(tab->name, gbuf, l - 1) == 0)
                    break;                  /* found */
            } else
            /* test the full opcode name */
            if (sim_strcasecmp(tab->name, gbuf) == 0) 
                break;                      /* found */
        }
        if (tab->name == NULL)              /* see if anything found */
            return SCPE_ARG;                /* no, return invalid argument error */
        num = tab->opbase<<16;              /* get the base opcode value */

        /* process each instruction type */
        switch(i) {
        /* mem ref instruction */
        case TYPE_A:                        /* c r,[*]o[,x] */
        /* zero memory instruction */
        case TYPE_E:                        /* c [*]o[,x] */
            switch(gbuf[l]) {
            case 'B': num |= 0x80000; break;    /* byte, set F bit */
            case 'H': num |= 0x00001; break;    /* halfword */
            case 'W': num |= 0x00000; break;    /* word */
            case 'D': num |= 0x00002; break;    /* doubleword */
            default:
                return SCPE_ARG;            /* base op suffix error */
            }
            /* Fall through */

        /* BIx instructions or memory reference */
        case TYPE_D:                        /* r,[*]o[,x] */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip leading blanks */
            if (i != TYPE_E) {
                /* get reg number except for zero memory instruction */
                if (*cptr >= '0' || *cptr <= '7') { /* reg# is 0-7 */
                    x = *cptr++ - '0';      /* get the reg# */
                    while (sim_isspace(*cptr))
                        cptr++;             /* skip any blanks */
                    if (*cptr++ != ',')     /* check for required comma */
                        return SCPE_ARG;    /* anything else is an argument error */
                    num |= x << 23;         /* position reg number in instruction */
                } else 
                    return SCPE_ARG;        /* invalid reg number is an argument error */
            }
            /* Fall through */

        /* branch instruction */
        case TYPE_B:                        /* [*]o[,x] */
        if (*cptr == '*') {                 /* test for indirection */
            num |= 0x100000;                /* set indirect flag */
            cptr++;                         /* skip past the '*' */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip blanks */
        }
        if ((r = get_off(cptr, &tptr, 16, val, &mod)))  /* get operand address */
            return r;                       /* argument error if a problem */
        cptr = tptr;                        /* set pointer to returned next char pointer */
        if (*val > 0x7FFFF)                 /* 19 bit address max */
            return SCPE_ARG;                /* argument error */
        num |= *val;                        /* or our address into instruction */
        if (mod) {
            return SCPE_ARG;                /* if a '(' found, that is an arg error */
        }
        if (*cptr++ == ',') {               /* test for optional index reg number */
            if (*cptr >= '0' || *cptr <= '7') { /* reg# is 0-7 */
                x = *cptr++ - '0';          /* get reg number */
                num |= x << 20;             /* position and put into instruction */
            } else 
                return SCPE_ARG;            /* reg# not 0-7, so arg error */
        }
        break;

        /* immediate or XIO instruction */
        case TYPE_C:                        /* r,v */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip spaces */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* get reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                if (*cptr++ != ',')         /* next char need to be a comma */
                    return SCPE_ARG;        /* it's not, so arg error */
                num |= x << 23;             /* position and put into instruction */
            } else 
                return SCPE_ARG;            /* invalid reg#, so arg error */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, rdx, val)))   /* get 16 bit immediate value */
                return r;                   /* return error from conversion */
            num |= *val;                    /* or in the 16 bit value */
            break;

        /* reg-reg instructions */
        case TYPE_F:                        /* r,r */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip blanks */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                if (*cptr++ != ',')         /* test for required ',' */
                    return SCPE_ARG;        /* it's not there, so error */
                num |= x << 23;             /* insert first reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip more spaces */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip any spaces */
                num |= x << 20;             /* insert 2nd reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            break;

        /* single reg instructions */
        case TYPE_G:                        /* r */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip blanks */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                num |= x << 23;             /* insert first reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            break;

        /* opcode only instructions */
        case TYPE_H:                        /* empty */
            break;

        /* reg and bit shift instructions */
        case TYPE_I:                        /* r,b */
            while (sim_isspace (*cptr))
                cptr++;                     /* skip blanks */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                if (*cptr++ != ',')         /* test for required ',' */
                    return SCPE_ARG;        /* it's not there, so error */
                num |= (x << 23);           /* insert first reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, 10, val)))    /* get 5 bit shift value */
                return r;                   /* return error from conversion */
            if (*val > 0x1f)                /* 5 bit max count */
                return SCPE_ARG;            /* invalid shift count */
            num |= (*val << 16);            /* or in the 5 bit value */
            break; 

        /* register bit operations */
        case TYPE_K:                        /* r,rb */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip blanks */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                if (*cptr++ != ',')         /* test for required ',' */
                    return SCPE_ARG;        /* it's not there, so error */
                num |= (x << 20);           /* insert reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, 10, val)))    /* get 5 bit bit number */
                return r;                   /* return error from conversion */
            if (*val > 0x1f)                /* 5 bit max count */
                return SCPE_ARG;            /* invalid bit count */
            x = *val / 8;                   /* get 2 bit byte number */
            num |= (x & 3) << 16;           /* insert 2 bit byte code into instruction */
            x = *val % 8;                   /* get bit in byte value */
            num |= (x & 7) << 23;           /* or in the bit value */
            break; 

        /* interrupt control instructions */
        case TYPE_L:                        /* i */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, rdx, val)))   /* get 7 bit bit number */
                return r;                   /* return error from conversion */
            if (*val > 0x7f)                /* 7 bit max count */
                return SCPE_ARG;            /* invalid value */
            num |= (*val & 0x7f) << 19;     /* or in the interrupt level */
            break;

        /* CD/TD Class E I/O instructions */
        case TYPE_M:                        /* d,v */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, rdx, val)))   /* get 7 bit bit number */
                return r;                   /* return error from conversion */
            if (*val > 0x7f)                /* 7 bit max count */
                return SCPE_ARG;            /* invalid value */
            num |= (*val & 0x7f) << 19;     /* or in the device address */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, rdx, val)))   /* get 16 bit command code */
                return r;                   /* return error from conversion */
            num |= *val;                    /* or in the 16 bit value */
            break;
        }
        return (tab->type & H) ? 2 : 4;     /* done with nonbased instructions */
    }

    /* see if we are processing a base mode instruction */
    if (sw & SWMASK ('M')) {                /* base mode? */
        /* process base mode instruction */
        cptr = get_glyph(cptr, gbuf, 0);    /* Get uppercase opcode */
        l = strlen(gbuf);                   /* save the num of char in opcode */
        /* loop through the instruction table for an opcode match and get the type */ 
        for (tab = optab; tab->name != NULL; tab++) {
            i = tab->type & 0xf;            /* get the type */
            /* check for memory reference instruction */
            if (i == TYPE_A || i == TYPE_E) {
                /* test for base opcode name without B, H, W, D applied */
                if (sim_strncasecmp(tab->name, gbuf, l - 1) == 0)
                    break;                  /* found */
            } else
            /* test the full opcode name */
            if (sim_strcasecmp(tab->name, gbuf) == 0) 
                break;                      /* found */
        }
        if (tab->name == NULL)              /* see if anything found */
            return SCPE_ARG;                /* no, return invalid argument error */
        num = tab->opbase<<16;              /* get the base opcode value */

        /* process each instruction type */
        switch(i) {
        /* mem ref instruction */
        case TYPE_A:                        /* c r,o[(b)][,x] */
        /* zero memory instruction */
        case TYPE_E:                        /* c o[(b)][,x] */
        switch(gbuf[l]) {
            case 'B': num |= 0x80000; break;    /* byte, set F bit */
            case 'H': num |= 0x00001; break;    /* halfword */
            case 'W': num |= 0x00000; break;    /* word */
            case 'D': num |= 0x00002; break;    /* doubleword */
            default:
                return SCPE_ARG;            /* base op suffix error */
        }
        /* Fall through */

        /* BIx instructions or memory reference */
        case TYPE_D:                        /* r,o[(b)],[,x] */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip leading blanks */
            if (i != TYPE_E) {
                /* get reg number except for zero memory instruction */
                if (*cptr >= '0' || *cptr <= '7') { /* reg# is 0-7 */
                    x = *cptr++ - '0';      /* get the reg# */
                    while (sim_isspace(*cptr))
                        cptr++;             /* skip any blanks */
                    if (*cptr++ != ',')     /* check for required comma */
                        return SCPE_ARG;    /* anything else is an argument error */
                    num |= x << 23;         /* position reg number in instruction */
                } else 
                    return SCPE_ARG;        /* invalid reg number is an argument error */
            }
          /* Fall through */

        /* branch instruction */
        case TYPE_B:                        /* o[(b)],[,x] */
            if ((r = get_off(cptr, &tptr, 16, val, &mod)))  /* get offset */
                return r;                   /* argument error if a problem */
            cptr = tptr;                    /* set pointer to returned next char pointer */
            if (*val > 0xFFFF)              /* 16 bit offset max */
                return SCPE_ARG;            /* argument error */
            num |= *val;                    /* or offset into instruction */
            if (mod) {                      /* see if '(' found in input */
                if (*cptr >= '0' || *cptr <= '7') { /* base reg# 0-7 */
                    x = *cptr++ - '0';      /* get reg number */
                    while (sim_isspace(*cptr))
                        cptr++;             /* skip any spaces */
                    if (*cptr++ != ')')     /* test for closing right parend */
                        return SCPE_ARG;    /* arg error if not found */
                   num |= x << 16;          /* put base reg number into instruction */
                } else
                    return SCPE_ARG;        /* no '(' found, so arg error */
            }
            if (*cptr++ == ',') {           /* test for optional index reg number */
                if (*cptr >= '0' || *cptr <= '7') { /* reg# is 0-7 */
                    x = *cptr++ - '0';      /* get reg number */
                    num |= x << 20;         /* position and put into instruction */
                } else 
                    return SCPE_ARG;        /* reg# not 0-7, so arg error */
            }
            break;

        /* immediate or XIO instruction */
        case TYPE_C:                        /* r,v */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip spaces */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* get reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                if (*cptr++ != ',')         /* next char need to be a comma */
                    return SCPE_ARG;        /* it's not, so arg error */
                num |= x << 23;             /* position and put into instruction */
            } else 
                return SCPE_ARG;            /* invalid reg#, so arg error */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, rdx, val)))   /* get 16 bit immediate value */
                return r;                   /* return error from conversion */
            num |= *val;                    /* or in the 16 bit value */
            break;

        /* reg-reg instructions */
        case TYPE_F:                        /* r,r */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip blanks */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                if (*cptr++ != ',')         /* test for required ',' */
                    return SCPE_ARG;        /* it's not there, so error */
                num |= x << 23;             /* insert first reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip more spaces */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip any spaces */
                num |= x << 20;             /* insert 2nd reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            break;

        /* single reg instructions */
        case TYPE_G:                        /* r */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip blanks */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                num |= x << 23;             /* insert first reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            break;

        /* opcode only instructions */
        case TYPE_H:                        /* empty */
            break;

        /* reg and bit shift instructions */
        case TYPE_I:                        /* r,b */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip blanks */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                if (*cptr++ != ',')         /* test for required ',' */
                    return SCPE_ARG;        /* it's not there, so error */
                num |= (x << 23);           /* insert first reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, 10, val)))    /* get 5 bit shift value */
                return r;                   /* return error from conversion */
            if (*val > 0x1f)                /* 5 bit max count */
                return SCPE_ARG;            /* invalid shift count */
            num |= (*val << 16);            /* or in the 5 bit value */
            break; 

        /* register bit operations */
        case TYPE_K:                        /* r,rb */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip blanks */
            if (*cptr >= '0' || *cptr <= '7') { /* test for valid reg# */
                x = *cptr++ - '0';          /* calc reg# */
                while (sim_isspace(*cptr))
                    cptr++;                 /* skip spaces */
                if (*cptr++ != ',')         /* test for required ',' */
                    return SCPE_ARG;        /* it's not there, so error */
                num |= (x << 20);           /* insert reg# into instruction */
            } else 
                return SCPE_ARG;            /* reg# invalid, so arg error */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, 10, val)))    /* get 5 bit bit number */
                return r;                   /* return error from conversion */
            if (*val > 0x1f)                /* 5 bit max count */
                return SCPE_ARG;            /* invalid bit count */
            x = *val / 8;                   /* get 2 bit byte number */
            num |= (x & 3) << 16;           /* insert 2 bit byte code into instruction */
            x = *val % 8;                   /* get bit in byte value */
            num |= (x & 7) << 23;           /* or in the bit value */
            break; 

        /* interrupt control instructions */
        case TYPE_L:                        /* i */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, rdx, val)))   /* get 7 bit bit number */
                return r;                   /* return error from conversion */
            if (*val > 0x7f)                /* 7 bit max count */
                return SCPE_ARG;            /* invalid value */
            num |= (*val & 0x7f) << 19;     /* or in the interrupt level */
            break;

        /* CD/TD Class E I/O instructions */
        case TYPE_M:                        /* d,v */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, rdx, val)))   /* get 7 bit bit number */
                return r;                   /* return error from conversion */
            if (*val > 0x7f)                /* 7 bit max count */
                return SCPE_ARG;            /* invalid value */
            num |= (*val & 0x7f) << 19;     /* or in the device address */
            while (sim_isspace(*cptr))
                cptr++;                     /* skip any blanks */
            if ((r = get_imm(cptr, &tptr, rdx, val)))   /* get 16 bit command code */
                return r;                   /* return error from conversion */
            num |= *val;                    /* or in the 16 bit value */
            break;
        }
        return (tab->type & H) ? 2 : 4;     /* done with base mode insrructions */
    }

    /* get here for any other switch value */
    /* this code will get a value based on length specified in switches */
    num = get_uint(cptr, rdx, max[l], &r);  /* get the unsigned value */
    for (i = 0; i < l && i < 4; i++) 
        val[i] = (num >> ((l - (1 + i)) * 8)) & 0xff; /* get 1-4 bytes of data */
    return -(l-1);
}

