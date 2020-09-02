/* i7090_sys.c: IBM 7090 Simulator system interface.

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

#ifdef I704
char                sim_name[] = "IBM 704";
#else
char                sim_name[] = "IBM 7090";
#endif

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 1;

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &chan_dev,
#ifdef CPANEL
    &cp_dev,
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
#if NUM_DEVS_MT > 0
    &mta_dev,
#if NUM_DEVS_MT > 1
    &mtb_dev,
#if NUM_DEVS_MT > 2
    &mtc_dev,
#if NUM_DEVS_MT > 3
    &mtd_dev,
#if NUM_DEVS_MT > 4
    &mte_dev,
#if NUM_DEVS_MT > 5
    &mtf_dev,
#endif /* 5 */
#endif /* 4 */
#endif /* 3 */
#endif /* 2 */
#endif /* 1 */
#endif /* 0 */
#ifdef MT_CHANNEL_ZERO
    &mtz_dev,
#endif
#if NUM_DEVS_HT > 0
    &hta_dev,
#if NUM_DEVS_HT > 1
    &htb_dev,
#endif
#endif
#ifdef NUM_DEVS_HD
    &hsdrm_dev,
#endif
#ifdef NUM_DEVS_DR
    &drm_dev,
#endif
#ifdef NUM_DEVS_DSK
    &dsk_dev,
#endif
#ifdef NUM_DEVS_COM
    &coml_dev,
    &com_dev,
#endif
#ifdef NUM_DEVS_CHRON
    &chron_dev,
#endif
    NULL
};

/* Device addressing words */
#ifdef NUM_DEVS_DR
DIB  drm_dib = { CH_TYP_PIO, 1, 0301, 0760, &drm_cmd, &drm_ini };
#endif
#ifdef NUM_DEVS_CDP
DIB  cdp_dib = { CH_TYP_PIO|CH_TYP_76XX, 1, 0341, 0777, &cdp_cmd, &cdp_ini };
#endif
#ifdef NUM_DEVS_CDR
DIB  cdr_dib = { CH_TYP_PIO|CH_TYP_76XX, 1, 0321, 0777, &cdr_cmd, NULL };
#endif
#ifdef NUM_DEVS_LPR
DIB  lpr_dib = { CH_TYP_PIO|CH_TYP_76XX, 1, 0361, 0774, &lpr_cmd, &lpr_ini };
#endif
#if (NUM_DEVS_MT > 0) || defined(MT_CHANNEL_ZERO)
DIB  mt_dib = { CH_TYP_PIO|CH_TYP_76XX, NUM_UNITS_MT, 0200, 0740, &mt_cmd,
                                                                &mt_ini };
#endif
#ifdef NUM_DEVS_CHRON
DIB  chron_dib = { CH_TYP_PIO|CH_TYP_76XX, 1, 0200, 0740, &chron_cmd, NULL };
#endif
#ifdef NUM_DEVS_DSK
DIB  dsk_dib = { CH_TYP_79XX, 0, 0, 0, &dsk_cmd, &dsk_ini };
#endif
#ifdef NUM_DEVS_HT
DIB  ht_dib = { CH_TYP_79XX, NUM_UNITS_HT, 0, 0, &ht_cmd, NULL };
#endif
#ifdef NUM_DEVS_COM
DIB  com_dib = { CH_TYP_79XX, 0, 0, 0, &com_cmd, NULL };
#endif
#ifdef NUM_DEVS_HD
DIB  hsdrm_dib = { CH_TYP_SPEC, 1, 0330, 0777, &hsdrm_cmd, &hsdrm_ini };
#endif


/* Simulator stop codes */
const char         *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "IO device not ready",
    "HALT instruction",
    "Breakpoint",
    "Unknown Opcode",
    "Nested indirects exceed limit",
    "Nested XEC's exceed limit",
    "I/O check error",
    "Memory management trap during trap",
    "7750 invalid line number",
    "7750 invalid message",
    "7750 No free output buffers",
    "7750 No free input buffers", "Error?", "Error2", 0
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
    {"CTSS", DEBUG_CTSS},
    {"PROT", DEBUG_PROT},
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


/* Character conversion tables */
const char          mem_to_ascii[64] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'b', '=', '\'', ':', '>', '%',    /* 17 = box */
    '+', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '@',     /* 37 = stop code */
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '^',     /* 57 = triangle */
    ' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '@', ',', '(', '~', '\\', '#'
};                              /* 72 = rec mark */
                                /* 75 = squiggle, 77 = del */

const char          ascii_to_mem[128] = {
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,     /* 0 - 37 */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    060, 052,  -1, 077, 053, 017,  -1, 014,     /* 40 - 77 */
    074, 034, 054, 020, 073, 040, 033, 061,
    000, 001, 002, 003, 004, 005, 006, 007,
    010, 011, 015, 056, 036, 013, 016, 072,
    037, 021, 022, 023, 024, 025, 026, 027,     /* 100 - 137 */
    030, 031, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 062, 063, 064, 065, 066,
    067, 070, 071, 035, 076, 055, 057, 012,
    000, 021, 022, 023, 024, 025, 026, 027,     /* 140 - 177 */
    030, 031, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 062, 063, 064, 065, 066,
    067, 070, 071,  -1,  -1,  -1,  -1,  -1
};


/* Load a card image file into memory.  */

t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    t_uint64            wd;
    t_uint64            mask;
    uint8               buffer[160];
    int                 addr = 0;
    int                 dlen = 0;
    char               *p;
    int                 i, j;

    if (match_ext(fnam, "crd")) {
        int                 firstcard = 1;
        uint16              image[80];
        t_uint64            lbuff[24];

        while (sim_fread(buffer, 1, 160, fileref) == 160) {
            /* Convert bits into image */
            for (j = i = 0; j < 80; j++) {
                image[j] = buffer[i++];
                image[j] |= buffer[i++] << 8;
            }

            /* Bit flip into read buffer */
            for (i = 0; i < 24; i++) {
                int                 bit = 1 << (i / 2);
                int                 b = 36 * (i & 1);
                int                 col;

                mask = 1;
                wd = 0;
                for (col = 35; col >= 0; mask <<= 1) {
                    if (image[col-- + b] & bit)
                        wd |= mask;
                }
                lbuff[i] = wd;
            }
            if (firstcard) {
                addr = 0;
                dlen = 3 + (int)((lbuff[0] >> 18) & AMASK);
                firstcard = 0;
                i = 0;
            } else if (dlen == 0) {
                addr = (int)(lbuff[0] & AMASK);
                dlen = (int)(lbuff[0] >> 18) & AMASK;
                i = 2;
            } else
                i = 0;
            for (; i < 24 && dlen > 0; i++) {
                M[addr++] = lbuff[i];
                dlen--;
            }
        }
    } else if (match_ext(fnam, "cbn")) {
        int                 firstcard = 1;
        uint16              image[80];
        t_uint64            lbuff[24];

        while (sim_fread(buffer, 1, 160, fileref) == 160) {
            /* Convert bits into image */
            for (j = i = 0; j < 80; j++) {
                image[j] = buffer[i++] & 077;
                image[j] |= (buffer[i++] & 077) << 6;
            }

            /* Bit flip into read buffer */
            for (i = 0; i < 24; i++) {
                int                 bit = 1 << (i / 2);
                int                 b = 36 * (i & 1);
                int                 col;

                mask = 1;
                wd = 0;
                for (col = 35; col >= 0; mask <<= 1) {
                    if (image[col-- + b] & bit)
                        wd |= mask;
                }
                lbuff[i] = wd;
            }
            if (firstcard) {
                addr = 0;
                dlen = 3 + (int)((lbuff[0] >> 18) & AMASK);
                firstcard = 0;
                i = 0;
            } else if (dlen == 0) {
                addr = (int)(lbuff[0] & AMASK);
                dlen = (int)(lbuff[0] >> 18) & AMASK;
                i = 2;
            } else
                i = 0;
            for (; i < 24 && dlen > 0; i++) {
                M[addr++] = lbuff[i];
                dlen--;
            }
        }
     } else if (match_ext(fnam, "oct")) {
        while (fgets((char *)buffer, 80, fileref) != 0) {
             for(p = (char *)buffer; *p == ' ' || *p == '\t'; p++);
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

    } else if (match_ext(fnam, "sym")) {
        while (fgets((char *)buffer, 80, fileref) != 0) {
             for(p = (char *)buffer; *p == ' ' || *p == '\t'; p++);
            /* Grab address */
             for(addr = 0; *p >= '0' && *p <= '7'; p++)
                addr = (addr << 3) + *p - '0';
             while(*p == ' ' || *p == '\t') p++;
             if(sim_strncasecmp(p, "BCD", 3) == 0) {
                 p += 4;
                 parse_sym(++p, addr, &cpu_unit, &M[addr], SWMASK('C'));
             } else if (sim_strncasecmp(p, "OCT", 3) == 0) {
                p += 4;
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
    const char         *name;
    uint8               type;
}
t_opcode;

#define TYPE_A  0               /* Basic opcode format */
#define TYPE_B  1               /* OpcodeF Y[,T] */
#define TYPE_C  2               /* Opcode  Y,C */
#define TYPE_D  3               /* Opcode Y[,T] */
#define TYPE_E  4               /* Opcode */
#define TYPE_F  5               /* Opcode T */
#define TYPE_G  6               /* Opcode K */
#define TYPE_P  8               /* Positive 0760 instructs */
#define TYPE_N  9               /* Negitive 0760 instructs */
#define TYPE_X  10

/* Opcodes */
t_opcode            base_ops[] = {
    {OP_TXI, "TXI", TYPE_A},
    {OP_TIX, "TIX", TYPE_A},
    {OP_TXH, "TXH", TYPE_A},
    {OP_STR, "STR", TYPE_E},
    {OP_TNX, "TNX", TYPE_A},
    {OP_TXL, "TXL", TYPE_A},
    {0, NULL, TYPE_X}
};

/* Positive opcodes */
t_opcode            pos_ops[] = {
    {0760, "", TYPE_P},
    {OP_HTR, "HTR", TYPE_B},
    {OP_TRA, "TRA", TYPE_B},
    {OP_TTR, "TTR", TYPE_B},
    {OP_TRCA, "TRCA", TYPE_B},
    {OP_TRCC, "TRCC", TYPE_B},
    {OP_TRCE, "TRCE", TYPE_B},
    {OP_TRCG, "TRCG", TYPE_B},
    {OP_TEFA, "TEFA", TYPE_B},
    {OP_TEFC, "TEFC", TYPE_B},
    {OP_TEFE, "TEFE", TYPE_B},
    {OP_TEFG, "TEFG", TYPE_B},
    {OP_TLQ, "TLQ", TYPE_B},
    {OP_IIA, "IIA", TYPE_E},
    {OP_TIO, "TIO", TYPE_B},
    {OP_OAI, "OAI", TYPE_E},
    {OP_PAI, "PAI", TYPE_E},
    {OP_TIF, "TIF", TYPE_B},
    {OP_IIR, "IIR", TYPE_G},
    {OP_RFT, "RFT", TYPE_G},
    {OP_SIR, "SIR", TYPE_G},
    {OP_RNT, "RNT", TYPE_G},
    {OP_RIR, "RIR", TYPE_G},
    {OP_TCOA, "TCOA", TYPE_B},
    {OP_TCOB, "TCOB", TYPE_B},
    {OP_TCOC, "TCOC", TYPE_B},
    {OP_TCOD, "TCOD", TYPE_B},
    {OP_TCOE, "TCOE", TYPE_B},
    {OP_TCOF, "TCOF", TYPE_B},
    {OP_TCOG, "TCOG", TYPE_B},
    {OP_TCOH, "TCOH", TYPE_B},
    {OP_TSX, "TSX", TYPE_D},
    {OP_TZE, "TZE", TYPE_B},
    {OP_CVR, "CVR", TYPE_C},
    {OP_TPL, "TPL", TYPE_B},
    {OP_XCA, "XCA", TYPE_E},
    {OP_TOV, "TOV", TYPE_B},
    {OP_TQP, "TQP", TYPE_B},
    {OP_TQO, "TQO", TYPE_B},
    {OP_MPY, "MPY", TYPE_B},
    {OP_VLM, "VLM", TYPE_C},
    {OP_DVH, "DVH", TYPE_B},
    {OP_DVP, "DVP", TYPE_B},
    {OP_VDH, "VDH", TYPE_C},
    {OP_VDP, "VDP", TYPE_C},
    {OP_FDH, "FDH", TYPE_B},
    {OP_FDP, "FDP", TYPE_B},
    {OP_FMP, "FMP", TYPE_B},
    {OP_DFMP, "DFMP", TYPE_B},
    {OP_FAD, "FAD", TYPE_B},
    {OP_DFAD, "DFAD", TYPE_B},
    {OP_FSB, "FSB", TYPE_B},
    {OP_DFSB, "DFSB", TYPE_B},
    {OP_FAM, "FAM", TYPE_B},
    {OP_DFAM, "DFAM", TYPE_B},
    {OP_FSM, "FSM", TYPE_B},
    {OP_DFSM, "DFSM", TYPE_B},
    {OP_ANS, "ANS", TYPE_B},
    {OP_ERA, "ERA", TYPE_B},
    {OP_CAS, "CAS", TYPE_B},
    {OP_ACL, "ACL", TYPE_B},
    {OP_HPR, "HPR", TYPE_E},
    {OP_OSI, "OSI", TYPE_B},
    {OP_ADD, "ADD", TYPE_B},
    {OP_ADM, "ADM", TYPE_B},
    {OP_SUB, "SUB", TYPE_B},
    {OP_IIS, "IIS", TYPE_B},
    {OP_LDI, "LDI", TYPE_B},
    {OP_DLD, "DLD", TYPE_B},
    {OP_ONT, "ONT", TYPE_B},
    {OP_RIS, "RIS", TYPE_B},
    {OP_OFT, "OFT", TYPE_B},
    {OP_CLA, "CLA", TYPE_B},
    {OP_CLS, "CLS", TYPE_B},
    {OP_ZET, "ZET", TYPE_B},
    {OP_XEC, "XEC", TYPE_B},
    {OP_LXA, "LXA", TYPE_D},
    {OP_LAC, "LAC", TYPE_D},
    {OP_ECA, "ECA", TYPE_B},
    {OP_LRI, "LRI", TYPE_B},
    {OP_RSCA, "RSCA", TYPE_B},
    {OP_RSCC, "RSCC", TYPE_B},
    {OP_RSCE, "RSCE", TYPE_B},
    {OP_RSCG, "RSCG", TYPE_B},
    {OP_STCA, "STCA", TYPE_B},
    {OP_STCC, "STCC", TYPE_B},
    {OP_STCE, "STCE", TYPE_B},
    {OP_STCG, "STCG", TYPE_B},
    {OP_LDA, "LDA", TYPE_B},
    {OP_LDQ, "LDQ", TYPE_B},
    {OP_ENB, "ENB", TYPE_B},
    {OP_STZ, "STZ", TYPE_B},
    {OP_STO, "STO", TYPE_B},
    {OP_SLW, "SLW", TYPE_B},
    {OP_STI, "STI", TYPE_B},
    {OP_STA, "STA", TYPE_B},
    {OP_STD, "STD", TYPE_B},
    {OP_STT, "STT", TYPE_B},
    {OP_STP, "STP", TYPE_B},
    {OP_SXA, "SXA", TYPE_D},
    {OP_SCA, "SCA", TYPE_D},
    {OP_TIA, "TIA", TYPE_B},
    {OP_SCHA, "SCHA", TYPE_B},
    {OP_SCHC, "SCHC", TYPE_B},
    {OP_SCHE, "SCHE", TYPE_B},
    {OP_SCHG, "SCHG", TYPE_B},
    {OP_SCDA, "SCDA", TYPE_B},
    {OP_SCDC, "SCDC", TYPE_B},
    {OP_SCDE, "SCDE", TYPE_B},
    {OP_SCDG, "SCDG", TYPE_B},
    {OP_ELD, "ELD", TYPE_B},
    {OP_EAD, "EAD", TYPE_B},
    {OP_EDP, "EDP", TYPE_B},
    {OP_EMP, "EMP", TYPE_B},
    {OP_PAX, "PAX", TYPE_F},
    {OP_PAC, "PAC", TYPE_F},
    {OP_PXA, "PXA", TYPE_F},
    {OP_PCA, "PCA", TYPE_F},
    {OP_CPY, "CPY", TYPE_B},
    {OP_NOP, "NOP", TYPE_E},
    {OP_RDS, "RDS", TYPE_D},
    {OP_BSR, "BSR", TYPE_D},
    {OP_LLS, "LLS", TYPE_D},
    {OP_LRS, "LRS", TYPE_D},
    {OP_WRS, "WRS", TYPE_D},
    {OP_ALS, "ALS", TYPE_D},
    {OP_WEF, "WEF", TYPE_D},
    {OP_ARS, "ARS", TYPE_D},
    {OP_REW, "REW", TYPE_D},
    {OP_AXT, "AXT", TYPE_D},
    {OP_DRS, "DRS", TYPE_D},
    {0, NULL, TYPE_X}
};

/* Negative opcodes */
t_opcode            neg_ops[] = {
    {04760, "", TYPE_N},
    {OP_TRCB, "TRCB", TYPE_B},
    {OP_TRCD, "TRCD", TYPE_B},
    {OP_TRCF, "TRCF", TYPE_B},
    {OP_TRCH, "TRCH", TYPE_B},
    {OP_TEFB, "TEFB", TYPE_B},
    {OP_TEFD, "TEFD", TYPE_B},
    {OP_TEFF, "TEFF", TYPE_B},
    {OP_TEFH, "TEFH", TYPE_B},
    {OP_RIA, "RIA", TYPE_B},
    {OP_PIA, "PIA", TYPE_E},
    {OP_IIL, "IIL", TYPE_G},
    {OP_LFT, "LFT", TYPE_G},
    {OP_SIL, "SIL", TYPE_G},
    {OP_LNT, "LNT", TYPE_G},
    {OP_RIL, "RIL", TYPE_G},
    {OP_TCNA, "TCNA", TYPE_B},
    {OP_TCNB, "TCNB", TYPE_B},
    {OP_TCNC, "TCNC", TYPE_B},
    {OP_TCND, "TCND", TYPE_B},
    {OP_TCNE, "TCNE", TYPE_B},
    {OP_TCNF, "TCNF", TYPE_B},
    {OP_TCNG, "TCNG", TYPE_B},
    {OP_TCNH, "TCNH", TYPE_B},
    {OP_ESNT, "ESNT", TYPE_B},
    {OP_TNZ, "TNZ", TYPE_B},
    {OP_CAQ, "CAQ", TYPE_C},
    {OP_TMI, "TMI", TYPE_B},
    {OP_XCL, "XCL", TYPE_E},
    {OP_TNO, "TNO", TYPE_B},
    {OP_CRQ, "CRQ", TYPE_C},
    {OP_DUFA, "DUFA", TYPE_B},
    {OP_DUAM, "DUAM", TYPE_B},
    {OP_DUFS, "DUFS", TYPE_B},
    {OP_DUSM, "DUSM", TYPE_B},
    {OP_DUFM, "DUFM", TYPE_B},
    {OP_DFDH, "DFDH", TYPE_B},
    {OP_DFDP, "DFDP", TYPE_B},
    {OP_MPR, "MPR", TYPE_B},
    {OP_UFM, "UFM", TYPE_B},
    {OP_UFA, "UFA", TYPE_B},
    {OP_UFS, "UFS", TYPE_B},
    {OP_UAM, "UAM", TYPE_B},
    {OP_USM, "USM", TYPE_B},
    {OP_ANA, "ANA", TYPE_B},
    {OP_LAS, "LAS", TYPE_B},
    {OP_SBM, "SBM", TYPE_B},
    {OP_CAL, "CAL", TYPE_B},
    {OP_ORA, "ORA", TYPE_B},
    {OP_NZT, "NZT", TYPE_B},
    {OP_LXD, "LXD", TYPE_D},
    {OP_LDC, "LDC", TYPE_D},
    {OP_RSCB, "RSCB", TYPE_B},
    {OP_RSCD, "RSCD", TYPE_B},
    {OP_RSCF, "RSCF", TYPE_B},
    {OP_RSCH, "RSCH", TYPE_B},
    {OP_STCB, "STCB", TYPE_B},
    {OP_STCD, "STCD", TYPE_B},
    {OP_STCF, "STCF", TYPE_B},
    {OP_STCH, "STCH", TYPE_B},
    {OP_STQ, "STQ", TYPE_B},
    {OP_ORS, "ORS", TYPE_B},
    {OP_DST, "DST", TYPE_B},
    {OP_SLQ, "SLQ", TYPE_B},
    {OP_STL, "STL", TYPE_B},
    {OP_SCD, "SCD", TYPE_D},
    {OP_SXD, "SXD", TYPE_D},
    {OP_SRI, "SRI", TYPE_B},
    {OP_SPI, "SPI", TYPE_B},
    {OP_LPI, "LPI", TYPE_B},
    {OP_PDX, "PDX", TYPE_F},
    {OP_PDC, "PDC", TYPE_F},
    {OP_ECQ, "ECQ", TYPE_B},
    {OP_TIB, "TIB", TYPE_B},
    {OP_SCHB, "SCHB", TYPE_B},
    {OP_SCHD, "SCHD", TYPE_B},
    {OP_SCHF, "SCHF", TYPE_B},
    {OP_SCHH, "SCHH", TYPE_B},
    {OP_SCDB, "SCDB", TYPE_B},
    {OP_SCDD, "SCDD", TYPE_B},
    {OP_SCDF, "SCDF", TYPE_B},
    {OP_SCDH, "SCDH", TYPE_B},
    {OP_ESB, "ESB", TYPE_B},
    {OP_EUA, "EUA", TYPE_B},
    {OP_EST, "EST", TYPE_B},
    {OP_PXD, "PXD", TYPE_F},
    {OP_PCD, "PCD", TYPE_F},
    {OP_LGL, "LGL", TYPE_D},
    {OP_BSF, "BSF", TYPE_D},
    {OP_LGR, "LGR", TYPE_D},
    {OP_CAD, "CAD", TYPE_B},
    {OP_SPOP, "RPQ", TYPE_B},
    {OP_RUN, "RUN", TYPE_D},
    {OP_RQL, "RQL", TYPE_D},
    {OP_AXC, "AXC", TYPE_D},
    {OP_TRS, "TRS", TYPE_D},
    {0, NULL, TYPE_X}
};

/* Positive 0760 opcodes */
t_opcode            pos_760[] = {
    {OP_CLM, "CLM", TYPE_E},
    {OP_RDCA, "RDCA", TYPE_E},
    {OP_RDCB, "RDCB", TYPE_E},
    {OP_RDCC, "RDCC", TYPE_E},
    {OP_RDCD, "RDCD", TYPE_E},
    {OP_RDCE, "RDCE", TYPE_E},
    {OP_RDCF, "RDCF", TYPE_E},
    {OP_RDCG, "RDCG", TYPE_E},
    {OP_RDCH, "RDCH", TYPE_E},
    {OP_RICA, "RICA", TYPE_E},
    {OP_RICB, "RICB", TYPE_E},
    {OP_RICC, "RICC", TYPE_E},
    {OP_RICD, "RICD", TYPE_E},
    {OP_RICE, "RICE", TYPE_E},
    {OP_RICF, "RICF", TYPE_E},
    {OP_RICG, "RICG", TYPE_E},
    {OP_RICH, "RICH", TYPE_E},
    {OP_BTTA, "BTTA", TYPE_E},
    {OP_BTTB, "BTTB", TYPE_E},
    {OP_BTTC, "BTTC", TYPE_E},
    {OP_BTTD, "BTTD", TYPE_E},
    {OP_BTTE, "BTTE", TYPE_E},
    {OP_BTTF, "BTTF", TYPE_E},
    {OP_BTTG, "BTTG", TYPE_E},
    {OP_BTTH, "BTTH", TYPE_E},
    {OP_LBT, "LBT", TYPE_E},
    {OP_CHS, "CHS", TYPE_E},
    {OP_SSP, "SSP", TYPE_E},
    {OP_ENK, "ENK", TYPE_E},
    {OP_IOT, "IOT", TYPE_E},
    {OP_COM, "COM", TYPE_E},
    {OP_ETM, "ETM", TYPE_E},
    {OP_RND, "RND", TYPE_E},
    {OP_FRN, "FRN", TYPE_E},
    {OP_DCT, "DCT", TYPE_E},
    {OP_RCT, "RCT", TYPE_E},
    {OP_LMTM, "LMTM", TYPE_E},
    {OP_SLF, "SLF", TYPE_E},
    {OP_SLN1, "SLN1", TYPE_E},
    {OP_SLN2, "SLN2", TYPE_E},
    {OP_SLN3, "SLN3", TYPE_E},
    {OP_SLN4, "SLN4", TYPE_E},
    {OP_SLN5, "SLN5", TYPE_E},
    {OP_SLN6, "SLN6", TYPE_E},
    {OP_SLN7, "SLN7", TYPE_E},
    {OP_SLN8, "SLN8", TYPE_E},
    {OP_SWT1, "SWT1", TYPE_E},
    {OP_SWT2, "SWT2", TYPE_E},
    {OP_SWT3, "SWT3", TYPE_E},
    {OP_SWT4, "SWT4", TYPE_E},
    {OP_SWT5, "SWT5", TYPE_E},
    {OP_SWT6, "SWT6", TYPE_E},
    {OP_PSE, "PSE", TYPE_E},
    {0, NULL, TYPE_X}
};

/* Negative 0760 opcodes */
t_opcode            neg_760[] = {
    {OP_ETTA, "ETTA", TYPE_E},
    {OP_ETTB, "ETTB", TYPE_E},
    {OP_ETTC, "ETTC", TYPE_E},
    {OP_ETTD, "ETTD", TYPE_E},
    {OP_ETTE, "ETTE", TYPE_E},
    {OP_ETTF, "ETTF", TYPE_E},
    {OP_ETTG, "ETTG", TYPE_E},
    {OP_ETTH, "ETTH", TYPE_E},
    {OP_PBT, "PBT", TYPE_E},
    {OP_EFTM, "EFTM", TYPE_E},
    {OP_SSM, "SSM", TYPE_E},
    {OP_LFTM, "LFTM", TYPE_E},
    {OP_ESTM, "ESTM", TYPE_E},
    {OP_ECTM, "ECTM", TYPE_E},
    {OP_LTM, "LTM", TYPE_E},
    {OP_EMTM, "EMTM", TYPE_E},
    {OP_RTT, "RTT", TYPE_E},
    {OP_ETT, "ETT", TYPE_E},
    {OP_SLT1, "SLT1", TYPE_E},
    {OP_SLT2, "SLT2", TYPE_E},
    {OP_SLT3, "SLT3", TYPE_E},
    {OP_SLT4, "SLT4", TYPE_E},
    {OP_SLT5, "SLT5", TYPE_E},
    {OP_SLT6, "SLT6", TYPE_E},
    {OP_SLT7, "SLT7", TYPE_E},
    {OP_SLT8, "SLT8", TYPE_E},
    {OP_SWT7, "SWT7", TYPE_E},
    {OP_SWT8, "SWT8", TYPE_E},
    {OP_SWT9, "SWT9", TYPE_E},
    {OP_SWT10, "SWT10", TYPE_E},
    {OP_SWT11, "SWT11", TYPE_E},
    {OP_SWT12, "SWT12", TYPE_E},
    {OP_MSE, "MSE", TYPE_D},
    {0, NULL, TYPE_X}
};

const char *chname[11] = {
    "*", "A", "B", "C", "D", "E", "F", "G", "H"
};

void
lookup_sopcode(FILE * of, t_value val, t_opcode * tab)
{
    uint16              op = (uint16)(val & 07777);

    while (tab->name != NULL) {
        if (tab->opbase == op) {
            fputs(tab->name, of);
            switch (tab->type) {
            case TYPE_D:
                fputc(' ', of);
                fprint_val(of, val & AMASK, 8, 14, PV_RZRO);
                if ((val & TMASK) != 0) {
                    fputc(',', of);
                    fputc('0' + (7 & (int)(val >> 15)), of);
                }
                return;
            case TYPE_E:
                if ((val & TMASK) != 0) {
                    fputc(' ', of);
                    fputc('0' + (7 & (int)(val >> 15)), of);
                }
                return;
            default:
                return;
            }
        }
        tab++;
    }
    tab--;
    fputs(tab->name, of);
    fputc(' ', of);
    fprint_val(of, val & AMASK, 8, 14, PV_RZRO);
    if ((val & TMASK) != 0) {
        fputc(',', of);
        fputc('0' + (7 & (int)(val >> 15)), of);
    }
}

void
lookup_opcode(FILE * of, t_value val, t_opcode * tab)
{
    uint16              op = (uint16)(val >> 24) & 07777;

    while (tab->name != NULL) {
        if (tab->opbase == op) {
            fputs(tab->name, of);
            switch (tab->type) {
            case TYPE_B:
                if ((val & 0000060000000LL) == 0000060000000LL)
                    fputc('*', of);
                fputc(' ', of);
                fprint_val(of, val & AMASK, 8, 14, PV_RZRO);
                if ((val & TMASK) != 0) {
                    fputc(',', of);
                    fputc('0' + (7 & (int)(val >> 15)), of);
                }
                return;
            case TYPE_C:
                fputc(' ', of);
                fprint_val(of, val & AMASK, 8, 14, PV_RZRO);
                fputc(',', of);
                fprint_val(of, (val >> 17) & 0000777LL, 8, 9, PV_RZRO);
                if ((val & TMASK) != 0) {
                    fputc(',', of);
                    fputc('0' + (7 & (int)(val >> 15)), of);
                }
                return;
            case TYPE_D:
                fputc(' ', of);
                fprint_val(of, val & AMASK, 8, 14, PV_RZRO);
                if ((val & TMASK) != 0) {
                    fputc(',', of);
                    fputc('0' + (7 & (int)(val >> 15)), of);
                }
                return;
            case TYPE_E:
                return;
            case TYPE_F:
                fputc(' ', of);
                fprint_val(of, val & AMASK, 8, 14, PV_RZRO);
                fputc(',', of);
                fputc('0' + (7 & (int)(val >> 15)), of);
                return;
            case TYPE_G:
                fputc(' ', of);
                fprint_val(of, val & RMASK, 8, 18, PV_RZRO);
                return;
            case TYPE_P:
                lookup_sopcode(of, val, pos_760);
                return;
            case TYPE_N:
                lookup_sopcode(of, val, neg_760);
                return;
            default:
                return;
            }
        }
        tab++;
    }
    fprintf(of, " %o Unknown opcode", op);
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

t_stat
fprint_sym(FILE * of, t_addr addr, t_value * val, UNIT * uptr, int32 sw)
{
    t_uint64            inst = *val;

/* Print value in octal first */
    fputc(' ', of);
    if (inst & MSIGN)
        fputc('-', of);
    else
        fputc(' ', of);
    fprint_val(of, inst & PMASK, 8, 35, PV_RZRO);

    if (sw & SWMASK('L')) {
        t_uint64        v;

        fputs("   L ", of);
        v = (inst >> 18) & AMASK;
        v = AMASK & ((AMASK ^ v) + 1);
        fprint_val(of, v, 8, 15, PV_RZRO);
        fputs(", ", of);
        v = AMASK & ((AMASK ^ inst) + 1);
        fprint_val(of, v, 8, 15, PV_RZRO);
    }

    if (sw & SWMASK('C')) {
        int                 i;

        fputs("   '", of);
        for (i = 5; i >= 0; i--) {
            int                 ch;

            ch = (int)(inst >> (6 * i)) & 077;
            fputc(mem_to_ascii[ch], of);
        }
        fputc('\'', of);
    }

    /* -m only valid on CPU */
    if ((uptr != NULL) && (uptr != &cpu_unit))
         return SCPE_ARG;     /* CPU? */
    if (sw & SWMASK('M')) {
        fputs("   ", of);
        switch (07 & (inst >> 33)) {
        case OP_TXI:
            fputs("TXI ", of);
            goto type_a;
        case OP_TIX:
            fputs("TIX ", of);
            goto type_a;
        case OP_TXH:
            fputs("TXH ", of);
            goto type_a;
        case OP_STR:
            fputs("STR ", of);
            break;
        case OP_TNX:
            fputs("TNX ", of);
            goto type_a;
        case OP_TXL:
            fputs("TXL ", of);
            goto type_a;

          type_a:
            fprint_val(of, inst & AMASK, 8, 14, PV_RZRO);
            fputc(',', of);
            fputc('0' + (7 & (int)(inst >> 15)), of);
            fputc(',', of);
            fprint_val(of, (inst >> 18) & AMASK, 8, 14, PV_RZRO);
            break;
        case 04:
            lookup_opcode(of, inst, neg_ops);
            break;
        case 00:
            lookup_opcode(of, inst, pos_ops);
            break;
        }
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
    t_value             d, tag;
    int                 sign;
    char                buffer[100];
    t_stat              r;

    while (isspace(*cptr))
        cptr++;
    d = 0;
    if (sw & SWMASK('M')) {
        t_opcode           *op;

        sign = 0;
        /* Grab opcode */
        cptr = get_glyph(cptr, buffer, 0);
        /* Check for indirection */
        i = strlen(buffer);
        if (i > 0 && buffer[i-1] == '*') {
            buffer[i-1] = '\0';
            sign = 1;
        }
        if ((op = find_opcode(buffer, base_ops)) != 0) {
            d = (t_uint64) op->opbase << 33;
            if (sign)
                return STOP_UUO;
        } else if ((op = find_opcode(buffer, pos_ops)) != 0 ||
                   (op = find_opcode(buffer, neg_ops)) != 0) {
            d = (t_uint64) op->opbase << 24;
            if (sign)
                d |= 03LL << 22;
        } else if ((op = find_opcode(buffer, pos_760)) != 0) {
            d = 00760LL << 24;
            d += op->opbase;
        } else if ((op = find_opcode(buffer, neg_760)) != 0) {
            d = 04760LL << 24;
            d += op->opbase;
        } else {
            return STOP_UUO;
        }
        if (op->type == TYPE_E) {
            *val = d;
            return SCPE_OK;
        }

        /* Collect first argument if there is one */
        cptr = get_glyph(cptr, buffer, ',');
        tag =  get_uint (buffer, 8,
                        (op->type == TYPE_G) ? RMASK: AMASK, &r);
        if (r != SCPE_OK)
            return r;
        d += tag;
        if (*cptr != '\0') {
            tag = 0;
            /* Collect second argument if there is one */
            cptr = get_glyph(cptr, buffer, ',');
            if (buffer[0] != '\0') {
                tag =  get_uint (buffer, 8, 07LL, &r);
                if (r != SCPE_OK)
                    return r;
                d += tag << 15;
            }
            if (*cptr != '\0') {
                cptr = get_glyph(cptr, buffer, 0);
                if (buffer[0] != '\0') {
                    tag =  get_uint (buffer, 8, AMASK, &r);
                    if (r != SCPE_OK)
                        return r;
                    if (op->type == TYPE_C)
                        d += tag << 15;
                    else
                        d += tag << 18;
                }
            }
        }
        if (*cptr != '\0')
            return STOP_UUO;
        *val = d;
        return SCPE_OK;
    } else if (sw & SWMASK('C')) {
        i = 0;
        while (*cptr != '\0' && i < 6) {
            d <<= 6;
            if (ascii_to_mem[0177 & *cptr] != (const char)-1)
                d |= ascii_to_mem[0177 & *cptr];
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
        d = get_uint(cptr, 8, WMASK, &r);
        if (r != SCPE_OK)
            return r;
        if (sign)
            d |= MSIGN;
    }
    *val = d;
    return SCPE_OK;

}
