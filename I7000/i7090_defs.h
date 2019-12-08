/* i7090_defs.h: IBM 7090 simulator definitions

   Copyright (c) 2005, Richard Cornwell

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

#include "sim_defs.h"                                   /* simulator defns */
#include "i7000_defs.h"

#define PAMASK          (MAXMEMSIZE - 1)                /* physical addr mask */
#define MEM_ADDR_OK(x)  (((uint16) (x&077777)) < MEMSIZE)
#define ReadP(x)        (M[x])
#define WriteP(x,y)     if (MEM_ADDR_OK (x)) M[x] = y
extern t_uint64         M[MAXMEMSIZE];
extern t_uint64         assembly[NUM_CHAN];

/* Processor specific masks */
#define ONEBIT          00200000000000LL
#define PMASK           00377777777777LL
#define RMASK           00000000777777LL
#define LMASK           00777777000000LL
#define AMSIGN          02000000000000LL
#define AMMASK          01777777777777LL
#define AQSIGN          01000000000000LL
#define AQMASK          00777777777777LL
#define APSIGN          00400000000000LL
#define PREMASK         00700000000000LL
#define AMASK           00000000077777LL
#define TMASK           00000000700000LL
#define DMASK           00077777000000LL
#define MSIGN           00400000000000LL
#define WMASK           00777777777777LL
#define FPCMASK         00377000000000LL
#define FPMMASK         00000777777777LL
#define FPOBIT          00001000000000LL
#define FPNBIT          00000400000000LL
#define FPMQERR         00000001000000LL        /* Bit 17 */
#define FPACERR         00000002000000LL        /* Bit 16 */
#define FPOVERR         00000004000000LL        /* Bit 15 */
#define FPSPERR         00000010000000LL        /* Bit 14 */
#define FPDPERR         00000040000000LL        /* Bit 12 */

/* 7090 specific channel functions */
/* Reset the channel, clear any pending device */
void chan_rst(int chan, int type);

/* Issue a command to a channel */
int chan_cmd(uint16 dev, uint16 cmd);

/* Give channel a command, any executing command is aborted */
int chan_start(int chan, uint16 addr);

/* Give channel a new address to start working at */
int chan_load(int chan, uint16 addr);

/* return the channels current command address */
void chan_store(int chan, uint16 addr);

/* Nop for the momement */
void chan_store_diag(int chan, uint16 addr);

/* Channel data handling */
int chan_write(int chan, t_uint64 *data, int flags);
int chan_read(int chan, t_uint64 *data, int flags);

void chan_proc();

extern uint16    dev_pulse[NUM_CHAN];                   /* Device pulse */
#define PUNCH_1  000001
#define PUNCH_2  000002
#define PUNCH_M  000003
#define PRINT_I  000004
#define PRINT_1  000010
#define PRINT_2  000020
#define PRINT_3  000040
#define PRINT_4  000100
#define PRINT_5  000200
#define PRINT_6  000400
#define PRINT_7  001000
#define PRINT_8  002000
#define PRINT_9  004000
#define PRINT_10 010000
#define PRINT_M  017770

/* Opcodes */
#define OP_TXI  1
#define OP_TIX  2
#define OP_TXH  3
#define OP_STR  5
#define OP_TNX  6
#define OP_TXL  7

/* Positive opcodes */
#define OP_HTR  0000
#define OP_TRA  0020
#define OP_TTR  0021
#define OP_TRCA 0022
#define OP_TRCC 0024
#define OP_TRCE 0026
#define OP_TRCG 0027
#define OP_TEFA 0030
#define OP_TEFC 0031
#define OP_TEFE 0032
#define OP_TEFG 0033
#define OP_TLQ  0040
#define OP_IIA  0041
#define OP_TIO  0042
#define OP_OAI  0043
#define OP_PAI  0044
#define OP_TIF  0046
#define OP_IIR  0051
#define OP_RFT  0054
#define OP_SIR  0055
#define OP_RNT  0056
#define OP_RIR  0057
#define OP_TCOA 0060
#define OP_TCOB 0061
#define OP_TCOC 0062
#define OP_TCOD 0063
#define OP_TCOE 0064
#define OP_TCOF 0065
#define OP_TCOG 0066
#define OP_TCOH 0067
#define OP_TSX  0074
#define OP_TZE  0100
#define OP_TIA  0101
#define OP_CVR  0114
#define OP_TPL  0120
#define OP_XCA  0131
#define OP_TOV  0140
#define OP_TQP  0162
#define OP_TQO  0161
#define OP_MPY  0200
#define OP_VLM  0204
#define OP_DVH  0220
#define OP_DVP  0221
#define OP_VDH  0224
#define OP_VDP  0225
#define OP_FDH  0240
#define OP_FDP  0241
#define OP_FMP  0260
#define OP_DFMP 0261
#define OP_FAD  0300
#define OP_DFAD 0301
#define OP_FSB  0302
#define OP_DFSB 0303
#define OP_FAM  0304
#define OP_DFAM 0305
#define OP_FSM  0306
#define OP_DFSM 0307
#define OP_ANS  0320
#define OP_ERA  0322
#define OP_CAS  0340
#define OP_ACL  0361
#define OP_HPR  0420
#define OP_OSI  0442
#define OP_ADD  0400
#define OP_ADM  0401
#define OP_SUB  0402
#define OP_IIS  0440
#define OP_LDI  0441
#define OP_DLD  0443
#define OP_OFT  0444
#define OP_RIS  0445
#define OP_ONT  0446
#define OP_LDA  0460    /* 704 only */
#define OP_CLA  0500
#define OP_CLS  0502
#define OP_ZET  0520
#define OP_XEC  0522
#define OP_LXA  0534
#define OP_LAC  0535
#define OP_RSCA 0540
#define OP_RSCC 0541
#define OP_RSCE 0542
#define OP_RSCG 0543
#define OP_STCA 0544
#define OP_STCC 0545
#define OP_STCE 0546
#define OP_STCG 0547
#define OP_LDQ  0560
#define OP_ECA  0561
#define OP_LRI  0562
#define OP_ENB  0564
#define OP_STZ  0600
#define OP_STO  0601
#define OP_SLW  0602
#define OP_STI  0604
#define OP_STA  0621
#define OP_STD  0622
#define OP_STT  0625
#define OP_STP  0630
#define OP_SXA  0634
#define OP_SCA  0636
#define OP_SCHA 0640
#define OP_SCHC 0641
#define OP_SCHE 0642
#define OP_SCHG 0643
#define OP_SCDA 0644
#define OP_SCDC 0645
#define OP_SCDE 0646
#define OP_SCDG 0647
#define OP_ELD  0670
#define OP_EAD  0671
#define OP_EDP  0672
#define OP_EMP  0673
#define OP_CPY  0700    /* 704 only */
#define OP_PAX  0734
#define OP_PAC  0737
#define OP_PXA  0754
#define OP_PCA  0756
#define OP_NOP  0761
#define OP_RDS  0762
#define OP_LLS  0763
#define OP_BSR  0764
#define OP_LRS  0765
#define OP_WRS  0766
#define OP_ALS  0767
#define OP_WEF  0770
#define OP_ARS  0771
#define OP_REW  0772
#define OP_AXT  0774
#define OP_DRS  0775
#define OP_SDN  0776

/* Negative opcodes */
#define OP_ESNT 04021
#define OP_TRCB 04022
#define OP_TRCD 04024
#define OP_TRCF 04026
#define OP_TRCH 04027
#define OP_TEFB 04030
#define OP_TEFD 04031
#define OP_TEFF 04032
#define OP_TEFH 04033
#define OP_RIA  04042
#define OP_PIA  04046
#define OP_IIL  04051
#define OP_LFT  04054
#define OP_SIL  04055
#define OP_LNT  04056
#define OP_RIL  04057
#define OP_TCNA 04060
#define OP_TCNB 04061
#define OP_TCNC 04062
#define OP_TCND 04063
#define OP_TCNE 04064
#define OP_TCNF 04065
#define OP_TCNG 04066
#define OP_TCNH 04067
#define OP_TNZ  04100
#define OP_TIB  04101
#define OP_CAQ  04114
#define OP_TMI  04120
#define OP_XCL  04130
#define OP_TNO  04140
#define OP_CRQ  04154
#define OP_DUFA 04301
#define OP_DUAM 04305
#define OP_DUFS 04303
#define OP_DUSM 04307
#define OP_DUFM 04261
#define OP_DFDH 04240
#define OP_DFDP 04241
#define OP_MPR  04200
#define OP_UFM  04260
#define OP_UFA  04300
#define OP_UFS  04302
#define OP_UAM  04304
#define OP_USM  04306
#define OP_ANA  04320
#define OP_LAS  04340
#define OP_SBM  04400
#define OP_CAL  04500
#define OP_ORA  04501
#define OP_NZT  04520
#define OP_LXD  04534
#define OP_LDC  04535
#define OP_RSCB 04540
#define OP_RSCD 04541
#define OP_RSCF 04542
#define OP_RSCH 04543
#define OP_STCB 04544
#define OP_STCD 04545
#define OP_STCF 04546
#define OP_STCH 04547
#define OP_ECQ  04561
#define OP_LPI  04564
#define OP_STQ  04600
#define OP_SRI  04601
#define OP_ORS  04602
#define OP_DST  04603
#define OP_SPI  04604
#define OP_SLQ  04620
#define OP_STL  04625
#define OP_SCD  04636
#define OP_SXD  04634
#define OP_SCHB 04640
#define OP_SCHD 04641
#define OP_SCHF 04642
#define OP_SCHH 04643
#define OP_SCDB 04644
#define OP_SCDD 04645
#define OP_SCDF 04646
#define OP_SCDH 04647
#define OP_ESB  04671
#define OP_EUA  04672
#define OP_EST  04673
#define OP_CAD  04700   /* 704 only */
#define OP_PDX  04734
#define OP_PDC  04737
#define OP_PXD  04754
#define OP_PCD  04756
#define OP_SPOP 04761
#define OP_LGL  04763
#define OP_BSF  04764
#define OP_LGR  04765
#define OP_RUN  04772
#define OP_RQL  04773
#define OP_AXC  04774
#define OP_TRS  04775

/* Positive 0760 opcodes */
#define OP_CLM  000000
#define OP_LBT  000001
#define OP_CHS  000002
#define OP_SSP  000003
#define OP_ENK  000004
#define OP_IOT  000005
#define OP_COM  000006
#define OP_ETM  000007
#define OP_RND  000010
#define OP_FRN  000011
#define OP_DCT  000012
#define OP_RCT  000014
#define OP_LMTM 000016
#define OP_RDCA 001352
#define OP_RDCB 002352
#define OP_RDCC 003352
#define OP_RDCD 004352
#define OP_RDCE 005352
#define OP_RDCF 006352
#define OP_RDCG 007352
#define OP_RDCH 010352
#define OP_RICA 001350
#define OP_RICB 002350
#define OP_RICC 003350
#define OP_RICD 004350
#define OP_RICE 005350
#define OP_RICF 006350
#define OP_RICG 007350
#define OP_RICH 010350
#define OP_SLF  000140
#define OP_SLN1 000141
#define OP_SLN2 000142
#define OP_SLN3 000143
#define OP_SLN4 000144
#define OP_SLN5 000145
#define OP_SLN6 000146
#define OP_SLN7 000147
#define OP_SLN8 000150
#define OP_SWT1 000161
#define OP_SWT2 000162
#define OP_SWT3 000163
#define OP_SWT4 000164
#define OP_SWT5 000165
#define OP_SWT6 000166
#define OP_BTTA 001000
#define OP_BTTB 002000
#define OP_BTTC 003000
#define OP_BTTD 004000
#define OP_BTTE 005000
#define OP_BTTF 006000
#define OP_BTTG 007000
#define OP_BTTH 010000
#define OP_PSE  0

/* Negative 0760 opcodes */
#define OP_ETTA 001000
#define OP_ETTB 002000
#define OP_ETTC 003000
#define OP_ETTD 004000
#define OP_ETTE 005000
#define OP_ETTF 006000
#define OP_ETTG 007000
#define OP_ETTH 010000
#define OP_PBT  000001
#define OP_EFTM 000002
#define OP_SSM  000003
#define OP_LFTM 000004
#define OP_ESTM 000005
#define OP_ECTM 000006
#define OP_LTM  000007
#define OP_LSNM 000010
#define OP_ETT  000011
#define OP_RTT  000012
#define OP_EMTM 000016
#define OP_SLT1 000141
#define OP_SLT2 000142
#define OP_SLT3 000143
#define OP_SLT4 000144
#define OP_SLT5 000145
#define OP_SLT6 000146
#define OP_SLT7 000147
#define OP_SLT8 000150
#define OP_SWT7 000161
#define OP_SWT8 000162
#define OP_SWT9 000163
#define OP_SWT10 000164
#define OP_SWT11 000165
#define OP_SWT12 000166
#define OP_MSE  0

/* Special Ops -0761 */
#define OP_SEA  000041
#define OP_SEB  000042
#define OP_IFT  000043
#define OP_EFT  000044
#define OP_ESM  000140
#define OP_TSM  000141

