/* b5500_defs.h: Burroughs 5500 simulator definitions 

   Copyright (c) 2016, Richard Cornwell

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


#ifndef _B5500_H_
#define _B5500_H_

#include "sim_defs.h"                                   /* simulator defns */

/* Definitions for each supported CPU */

#define NUM_DEVS_CDR    2
#define NUM_DEVS_CDP    1
#define NUM_DEVS_LPR    2
#define NUM_DEVS_CON    1
#define NUM_DEVS_DR     2
#define NUM_DEVS_MT     16
#define NUM_DEVS_DSK    2
#define NUM_DEVS_DTC    1
#define NUM_CHAN        4
#define MAXMEMSIZE      32768
#define CHARSPERWORD    8

extern t_uint64         M[];                            /* Main Memory */
extern uint16           IAR;                            /* Interrupt pending register */
extern uint32           iostatus;                       /* Active device status register */
extern uint8            loading;                        /* System booting flag */

/* Memory */
#define MEMSIZE         (cpu_unit[0].capac)             /* actual memory size */
#define MEMMASK         (MEMSIZE - 1)                   /* Memory bits */


/* Debuging controls */
#define DEBUG_CHAN      0x0000001       /* Show channel fetchs */
#define DEBUG_TRAP      0x0000002       /* Show CPU Traps */
#define DEBUG_CMD       0x0000004       /* Show device commands */
#define DEBUG_DATA      0x0000008       /* Show data transfers */
#define DEBUG_DETAIL    0x0000010       /* Show details */
#define DEBUG_EXP       0x0000020       /* Show error conditions */
#define DEBUG_SNS       0x0000040       /* Shows sense data for 7909 devs */
#define DEBUG_CTSS      0x0000080       /* Shows CTSS specail instructions */
#define DEBUG_PROT      0x0000100       /* Protection traps */

extern DEBTAB dev_debug[];


/* Returns from device commands */
#define SCPE_BUSY       (1)     /* Device is active */
#define SCPE_NODEV      (2)     /* No device exists */

/* Symbol tables */
typedef struct _opcode
{
    uint16              op;
    uint8               type;
    const char          *name;
}
t_opcode;

/* I/O Command codes */
#define IO_RDS  1       /* Read record */
#define IO_BSR  2       /* Backspace one record */
#define IO_BSF  3       /* Backspace one file */
#define IO_WRS  4       /* Write one record */
#define IO_WEF  5       /* Write eof */
#define IO_REW  6       /* Rewind */
#define IO_DRS  7       /* Set unit offline */
#define IO_SDL  8       /* Set density low */
#define IO_SDH  9       /* Set density high */
#define IO_RUN  10      /* Rewind and unload unit */
#define IO_TRS  11      /* Check it unit ready */
#define IO_CTL  12      /* Io control device specific */
#define IO_RDB  13      /* Read backwards */
#define IO_SKR  14      /* Skip record forward */
#define IO_ERG  15      /* Erase next records from tape */


t_stat chan_reset(DEVICE *);
t_stat chan_boot(t_uint64);
int find_chan();
void chan_release(int);
void start_io();
void chan_set_end(int) ;
void chan_set_parity(int) ;
void chan_set_eof(int) ;
void chan_set_read(int) ;
void chan_set_wcflg(int) ;
void chan_set_gm(int) ;
void chan_set_error(int) ;
void chan_set_notrdy(int) ;
void chan_set_bot(int) ;
void chan_set_eot(int) ;
void chan_set_wrp(int) ;
void chan_set_blank(int) ;
void chan_set_wc(int, uint16);
int chan_write_char(int, uint8 *, int) ;
int chan_read_char(int, uint8 *, int) ;
int chan_read_disk(int, uint8 *, int) ;
int chan_write_drum(int, uint8 *, int) ;
int chan_read_drum(int, uint8 *, int) ;

extern uint8       parity_table[64];
extern uint8       mem_to_ascii[64];
extern const char  con_to_ascii[64];
extern const char  ascii_to_con[128];
extern t_stat      fprint_sym(FILE *, t_addr, t_value *, UNIT *, int32);
extern int32       tmxr_poll;

/* Generic devices common to all */
extern DEVICE      cpu_dev; 
extern UNIT        cpu_unit[]; 
extern REG         cpu_reg[];
extern DEVICE      chan_dev; 

/* Global device definitions */
#if (NUM_DEVS_CDR > 0) | (NUM_DEVS_CDP > 0)
extern DEVICE       cdr_dev; 
extern t_stat       card_cmd(uint16, uint16, uint8, uint16 *);
#endif

#if (NUM_DEVS_CDP > 0)
extern DEVICE       cdp_dev; 
#endif

#if (NUM_DEVS_LPR > 0)
extern DEVICE       lpr_dev; 
extern t_stat       lpr_cmd(uint16, uint16, uint8, uint16 *);
#endif

#if (NUM_DEVS_CON > 0)
extern DEVICE       con_dev; 
extern t_stat       con_cmd(uint16, uint16, uint8, uint16 *);
#endif

#if (NUM_DEVS_DTC > 0)
extern DEVICE      dtc_dev; 
extern t_stat      dtc_cmd(uint16, uint16, uint8, uint16 *);
#endif

#if (NUM_DEVS_DR > 0)   
extern DEVICE      drm_dev; 
extern t_stat      drm_cmd(uint16, uint16, uint8, uint16 *, uint8);
#endif

#if (NUM_DEVS_DSK > 0)
extern DEVICE      dsk_dev; 
extern t_stat      dsk_cmd(uint16, uint16, uint8, uint16 *);
extern DEVICE      esu_dev; 
#endif

#if (NUM_DEVS_MT > 0) 
extern DEVICE      mt_dev; 
extern t_stat      mt_cmd(uint16, uint16, uint8, uint16 *);
#endif  /* NUM_DEVS_MT */

/* Character codes */
#define CHR_ABLANK      000
#define CHR_MARK        CHR_ABLANK
#define CHR_1           001
#define CHR_2           002
#define CHR_3           003
#define CHR_4           004
#define CHR_5           005
#define CHR_6           006
#define CHR_7           007
#define CHR_8           010
#define CHR_9           011
#define CHR_0           012
#define CHR_EQ          013
#define CHR_QUOT        014     /* Also @ */
#define CHR_COL         015
#define CHR_GT          016
#define CHR_TRM         017
#define CHR_BLANK       020
#define CHR_SLSH        021
#define CHR_S           022
#define CHR_T           023
#define CHR_U           024
#define CHR_V           025
#define CHR_W           026
#define CHR_X           027
#define CHR_Y           030
#define CHR_Z           031
#define CHR_RM          032
#define CHR_COM         033
#define CHR_RPARN       034     /* Also % */
#define CHR_WM          035
#define CHR_BSLSH       036
#define CHR_UND         037
#define CHR_MINUS       040
#define CHR_J           041
#define CHR_K           042
#define CHR_L           043
#define CHR_M           044
#define CHR_N           045
#define CHR_O           046
#define CHR_P           047
#define CHR_Q           050
#define CHR_R           051
#define CHR_EXPL        052
#define CHR_DOL         053
#define CHR_STAR        054
#define CHR_LBRK        055
#define CHR_SEMI        056
#define CHR_CART        057
#define CHR_PLUS        060
#define CHR_A           061
#define CHR_B           062
#define CHR_C           063
#define CHR_D           064
#define CHR_E           065
#define CHR_F           066
#define CHR_G           067
#define CHR_H           070
#define CHR_I           071
#define CHR_QUEST       072
#define CHR_DOT         073
#define CHR_LPARN       074     /* Also Square */
#define CHR_RBRAK       075
#define CHR_LESS        076
#define CHR_GM          077

/* Word mode opcodes */
#define WMOP_LITC               00000   /* Load literal */
#define WMOP_OPDC               00002   /* Load operand */
#define WMOP_DESC               00003   /* Load Descriptor */
#define WMOP_OPR                00001   /* Operator */
#define WMOP_DEL                00065   /* Delete top of stack */
#define WMOP_NOP                00055   /* Nop operation */
#define WMOP_XRT                00061   /* Set Variant */
#define WMOP_ADD                00101   /* Add */
#define WMOP_DLA                00105   /* Double Precision Add */
#define WMOP_PRL                00111   /* Program Release */
#define WMOP_LNG                00115   /* Logical Negate */
#define WMOP_CID                00121   /* Conditional Integer Store Destructive */
#define WMOP_GEQ                00125   /* WMOP_B greater than or equal to A */
#define WMOP_BBC                00131   /* Branch Backward Conditional */
#define WMOP_BRT                00135   /* Branch Return */
#define WMOP_INX                00141   /* Index */
#define WMOP_ITI                00211   /* Interrogate interrupt */
#define WMOP_LOR                00215   /* Logical Or */
#define WMOP_CIN                00221   /* Conditional Integer Store non-destructive */
#define WMOP_GTR                00225   /* B Greater than A */
#define WMOP_BFC                00231   /* Branch Forward Conditional */
#define WMOP_RTN                00235   /* Return normal */
#define WMOP_COC                00241   /* Construct Operand Call */
#define WMOP_SUB                00301   /* Subtract */
#define WMOP_DLS                00305   /* WMOP_Double Precision Subtract */
#define WMOP_MUL                00401   /* Multiply */
#define WMOP_DLM                00405   /* Double Precision Multiply */
#define WMOP_RTR                00411   /* Read Timer */
#define WMOP_LND                00415   /* Logical And */
#define WMOP_STD                00421   /* B Store Destructive */
#define WMOP_NEQ                00425   /* B Not equal to A */
#define WMOP_SSN                00431   /* Set Sign Bit */
#define WMOP_XIT                00435   /* Exit */
#define WMOP_MKS                00441   /* Mark Stack */
#define WMOP_DIV                01001   /* Divide */
#define WMOP_DLD                01005   /* Double Precision Divide */
#define WMOP_COM                01011   /* Communication operator */
#define WMOP_LQV                01015   /* Logical Equivalence */
#define WMOP_SND                01021   /* B Store Non-destructive */
#define WMOP_XCH                01025   /* Exchange */
#define WMOP_CHS                01031   /* Change sign bit */
#define WMOP_RTS                01235   /* Return Special */
#define WMOP_CDC                01241   /* Construct descriptor call */
#define WMOP_FTC                01425   /* Transfer F Field to Core Field */
#define WMOP_MOP                02015   /* Reset Flag bit */
#define WMOP_LOD                02021   /* Load */
#define WMOP_DUP                02025   /* Duplicate */
#define WMOP_TOP                02031   /* Test Flag Bit */
#define WMOP_IOR                02111   /* I/O Release */
#define WMOP_LBC                02131   /* Word Branch Backward Conditional */
#define WMOP_SSF                02141   /* Set or Store S or F registers */
#define WMOP_HP2                02211   /* Halt P2 */
#define WMOP_LFC                02231   /* Word Branch Forward Conditional */
#define WMOP_ZP1                02411   /* Conditional Halt */
#define WMOP_TUS                02431   /* Interrogate Peripheral Status */
#define WMOP_LLL                02541   /* Link List Look-up */
#define WMOP_IDV                03001   /* Integer Divide Integer */
#define WMOP_SFI                03011   /* Store for Interrupt */
#define WMOP_SFT                03411   /* Store for Test */
#define WMOP_FTF                03425   /* Transfer F Field to F Field */
#define WMOP_MDS                04015   /* Set Flag Bit */
#define WMOP_IP1                04111   /* Initiate P1 */
#define WMOP_ISD                04121   /* Interger Store Destructive */
#define WMOP_LEQ                04125   /* B Less Than or Equal to A */
#define WMOP_BBW                04131   /* Banch Backward Conditional */
#define WMOP_IP2                04211   /* Initiate P2 */
#define WMOP_ISN                04221   /* Integer Store Non-Destructive */
#define WMOP_LSS                04225   /* B Less Than A */
#define WMOP_BFW                04231   /* Branch Forward Unconditional */
#define WMOP_IIO                04411   /* Initiate I/O */
#define WMOP_EQL                04425   /* B Equal A */
#define WMOP_SSP                04431   /* Reset Sign Bit */
#define WMOP_CMN                04441   /* Enter Character Mode In Line */
#define WMOP_IFT                05111   /* Test Initiate */
#define WMOP_CTC                05425   /* Transfer Core Field to Core Field */
#define WMOP_LBU                06131   /* Word Branch Backward Unconditional */
#define WMOP_LFU                06231   /* Word Branch Forward Unconditional */
#define WMOP_TIO                06431   /* Interrogate I/O Channels */
#define WMOP_RDV                07001   /* Remainder Divide */
#define WMOP_FBS                07031   /* Flag Bit Search */
#define WMOP_CTF                07425   /* Transfer Core Field to F Field */
#define WMOP_ISO                00045   /* Variable Field Isolate XX */
#define WMOP_CBD                00351   /* Non-Zero Field Branch Backward Destructive Xy */
#define WMOP_CBN                00151   /* Non-Zero Field Branch Backward Non-Destructive Xy */
#define WMOP_CFD                00251   /* Non-Zero Field Branch Forward Destructive Xy */
#define WMOP_CFN                00051   /* Non-Zero Field Branch Forward Non-Destructive Xy */
#define WMOP_DIA                00055   /* Dial A XX */
#define WMOP_DIB                00061   /* Dial B XX Upper 6 not Zero */
#define WMOP_TRB                00065   /* Transfer Bits XX */
#define WMOP_FCL                00071   /* Compare Field Low XX */
#define WMOP_FCE                00075   /* Compare Field Equal XX */

/* Character Mode */
#define CMOP_EXC                00000   /* CMOP_Exit Character Mode */
#define CMOP_CMX                00100   /* Exit Character Mode In Line */
#define CMOP_BSD                00002   /* Skip Bit Destiniation */
#define CMOP_BSS                00003   /* SKip Bit Source */
#define CMOP_RDA                00004   /* Recall Destination Address */
#define CMOP_TRW                00005   /* Transfer Words */
#define CMOP_SED                00006   /* Set Destination Address */
#define CMOP_TDA                00007   /* Transfer Destination Address */
#define CMOP_TBN                00012   /* Transfer Blanks for Non-Numerics */
#define CMOP_SDA                00014   /* Store Destination Address */
#define CMOP_SSA                00015   /* Store Source Address */
#define CMOP_SFD                00016   /* Skip Forward Destination */
#define CMOP_SRD                00017   /* Skip Reverse Destination */
#define CMOP_SES                00022   /* Set Source Address */
#define CMOP_TEQ                00024   /* Test for Equal */
#define CMOP_TNE                00025   /* Test for Not-Equal */
#define CMOP_TEG                00026   /* Test for Greater Or Equal */
#define CMOP_TGR                00027   /* Test For Greater */
#define CMOP_SRS                00030   /* Skip Reverse Source */
#define CMOP_SFS                00031   /* Skip Forward Source */
#define CMOP_TEL                00034   /* Test For Equal or Less */
#define CMOP_TLS                00035   /* Test For Less */
#define CMOP_TAN                00036   /* Test for Alphanumeric */
#define CMOP_BIT                00037   /* Test Bit */
#define CMOP_INC                00040   /* Increase Tally */
#define CMOP_STC                00041   /* Store Tally */
#define CMOP_SEC                00042   /* Set Tally */
#define CMOP_CRF                00043   /* Call repeat Field */
#define CMOP_JNC                00044   /* Jump Out Of Loop Conditional */
#define CMOP_JFC                00045   /* Jump Forward Conditional */
#define CMOP_JNS                00046   /* Jump out of loop unconditional */
#define CMOP_JFW                00047   /* Jump Forward Unconditional */
#define CMOP_RCA                00050   /* Recall Control Address */
#define CMOP_ENS                00051   /* End Loop */
#define CMOP_BNS                00052   /* Begin Loop */
#define CMOP_RSA                00053   /* Recall Source Address */
#define CMOP_SCA                00054   /* Store Control Address */
#define CMOP_JRC                00055   /* Jump Reverse Conditional */
#define CMOP_TSA                00056   /* Transfer Source Address */
#define CMOP_JRV                00057   /* Jump Reverse Unconditional */
#define CMOP_CEQ                00060   /* Compare Equal */
#define CMOP_CNE                00061   /* COmpare for Not Equal */
#define CMOP_CEG                00062   /* Compare For Greater Or Equal */
#define CMOP_CGR                00063   /* Compare For Greater */
#define CMOP_BIS                00064   /* Set Bit */
#define CMOP_BIR                00065   /* Reet Bit */
#define CMOP_OCV                00066   /* Output Convert */
#define CMOP_ICV                00067   /* Input Convert */
#define CMOP_CEL                00070   /* Compare For Equal or Less */
#define CMOP_CLS                00071   /* Compare for Less */
#define CMOP_FSU                00072   /* Field Subtract */
#define CMOP_FAD                00073   /* Field Add */
#define CMOP_TRP                00074   /* Transfer Program Characters */
#define CMOP_TRN                00075   /* Transfer Numeric */
#define CMOP_TRZ                00076   /* Transfer Zones */
#define CMOP_TRS                00077   /* Transfer Source Characters */

/* Error codes for Q */         /* P1           P2 */
#define MEM_PARITY      00001   /* 060          040 */
#define INVALID_ADDR    00002   /* 061          041 */
#define STK_OVERFL      00004   /* 062          042 */
#define COM_OPR         00040   /* 064 +00      044 */
#define PROG_REL        00050   /* 065 +01      045 */
#define CONT_BIT        00060   /* 066 +02      046 */
#define PRES_BIT        00070   /* 067 +03      047 */
#define FLAG_BIT        00100   /* 070 +04      050 */
#define INDEX_ERROR     00110   /* 071 +05      051 */
#define EXPO_UNDER      00120   /* 072 +06      052 */
#define EXPO_OVER       00130   /* 073 +07      053 */
#define INT_OVER        00140   /* 074 +10      054 */
#define DIV_ZERO        00150   /* 075 +11      055 */

/* Addresses for Interrupts */
#define INTER_TIME      022
#define IO_BUSY         023
#define KEY_REQ         024
#define PRT1_FINISH     025
#define PRT2_FINISH     026
#define IO1_FINISH      027
#define IO2_FINISH      030
#define IO3_FINISH      031
#define IO4_FINISH      032
#define INQ_REQ         033
#define SPEC_IRQ1       035
#define DSK1_RDCHK      036
#define DSK2_RDCHK      037
#define PARITY_ERR      060
#define INVADR_ERR      061
#define STK_OVR_LOC     062
#define COM_OPR_LOC     064
#define PROG_REL_LOC    065
#define CONT_BIT_LOC    066
#define PRES_BIT_LOC    067
#define FLAG_BIT_LOC    070
#define INDEX_BIT_LOC   071
#define EXP_UND_LOC     072
#define EXP_OVR_LOC     073
#define INT_OVR_LOC     074
#define DIV_ZER_LOC     075
#define PARITY_ERR2     040
#define INVADR_ERR2     041
#define STK_OVR_LOC2    042
#define COM_OPR_LOC2    044
#define PROG_REL_LOC2   045
#define CONT_BIT_LOC2   046
#define PRES_BIT_LOC2   047
#define FLAG_BIT_LOC2   050
#define INDEX_BIT_LOC2  051
#define EXP_UND_LOC2    052
#define EXP_OVR_LOC2    053
#define INT_OVR_LOC2    054
#define DIV_ZER_LOC2    055

/* IAR BITS */
#define IAR6            040     /* Set if IRQ from Q */
#define IAR5            020     /* Set if IRQ from P1 */
#define IAR4            010     /* Q bit 3 */
#define IAR3            004     /* Q bit 4 */
#define IAR2            002     /* Q bit 5 */
#define IAR1            001     /* Q bit 6 or Q bit 2 */
#define IAR0            000     /* Q bit 7 or Q bit 1 */

#define IRQ_0           000001  /* Interval Timer */
#define IRQ_1           000002  /* I/O Busy */
#define IRQ_2           000004  /* Keyboard Request */
#define IRQ_3           000010  /* Printer 1 Finished */
#define IRQ_4           000020  /* Printer 2 Finished */
#define IRQ_5           000040  /* I/O Finish 1 */
#define IRQ_6           000100  /* I/O Finish 2 */
#define IRQ_7           000200  /* I/O Finish 3 */
#define IRQ_10          000400  /* I/O Finish 4 */
#define IRQ_11          001000  /* P2 Busy */
#define IRQ_12          002000  /* Inquiry Request */
#define IRQ_13          004000  /* Special IRQ 1 */
#define IRQ_14          010000  /* Disk Read Check 1 */
#define IRQ_15          020000  /* Disk Read Check 2 */

/* Masks */
#define FLAG            04000000000000000LL     /* Operand Flag */
#define FWORD           03777777777777777LL     /* Full word mask */
#define MSIGN           02000000000000000LL     /* Operator Word */
#define ESIGN           01000000000000000LL
#define EXPO            00770000000000000LL
#define EXPO_V          39
#define MANT            00007777777777777LL
#define NORM            00007000000000000LL
#define ROUND           00004000000000000LL
#define PRESENT         01000000000000000LL     /* Oprand Type */
#define DFLAG           02000000000000000LL     /* Descriptor */
#define WCOUNT          00017770000000000LL
#define WCOUNT_V         30
#define INTEGR          00000002000000000LL
#define CONTIN          00000001000000000LL
#define CORE            00000000000077777LL
#define RFIELD          00077700000000000LL     /* Mark Stack Control Word */
#define RFIELD_V        27                      /* Shift off by 6 bits */
#define SMSFF           00000020000000000LL
#define SSALF           00000010000000000LL
#define SVARF           00000000100000000LL
#define SCWMF           00000000000100000LL
#define FFIELD          00000007777700000LL     
#define FFIELD_V        15
#define REPFLD          00000770000000000LL
#define REPFLD_V        30
#define MODEF           00200000000000000LL     /* Program Descriptor +FFIELD and CORE */
#define ARGF            00100000000000000LL
#define PROGF           00400000000000000LL
#define RGH             00340700000000000LL     /* Return Control Word +FFIELD and CORE */
#define RGH_V           33
#define RKV             00034070000000000LL    
#define RKV_V           30
#define RL              00003000000000000LL     /* Save L register */
#define RL_V            36
#define LMASK           00000000007777777LL
#define HMASK           00007777770000000LL
#define DEV_DRUM_RD     01000000000000000LL
#define DEVMASK         00760000000000000LL
#define D_MASK          00777777777777777LL     
#define DEV_V           40
#define DEV_WC          00017770000000000LL
#define DEV_WC_V        30
#define DEV_CMD         00000007777700000LL
#define DEV_CMD_V       15
#define DEV_INHTRF      00000004000000000LL
#define DEV_XXX         00000002000000000LL
#define DEV_XXY         00000001000000000LL
#define DEV_BIN         00000000400000000LL
#define DEV_BACK        00000000200000000LL
#define DEV_WCFLG       00000000100000000LL
#define DEV_IORD        00000000040000000LL
#define DEV_OPT         00000000007700000LL     /* Print Space, Disk Segments */
#define CORE            00000000000077777LL

#define DEV_BUSY        00000000000100000LL     /* D16 */
#define DEV_MEMPAR      00000000000200000LL     /* D17 */
#define DEV_NOTRDY      00000000000400000LL     /* D18 */
#define DEV_PARITY      00000000001000000LL     /* D19 */
#define DEV_ERROR       00000000002000000LL     /* D20 */
#define DEV_EOF         00000000004000000LL     /* D21 */
#define DEV_MEMERR      00000000010000000LL     /* D22 */
#define DEV_RESULT      00000000037700000LL
#define DEV_EOT         01000100001000000LL     
#define DEV_BOT         01000200001000000LL     
#define DEV_BLANK       01000400001000000LL     

#define DRUM1_DEV       004                     /* 00100  (4) */
#define DSK1_DEV        006                     /* 00110  (6) */
#define DRUM2_DEV       010                     /* 01000  (8) */
#define CARD1_DEV       012                     /* 01010 (10) */
#define DSK2_DEV        014                     /* 01100 (12) */
#define CARD2_DEV       016                     /* 01110 (14) */
#define DTC_DEV         020                     /* 10000 (16) */
#define PT1_DEV         022                     /* 10010 (20) */
#define PT2_DEV         024                     /* 10100 (22) */
#define PRT1_DEV        026                     /* 10110 (24) */
#define PRT2_DEV        032                     /* 11010 (26) */
#define SPO_DEV         036                     /* 11110 (30) */
#define DRUM1_FLAG      00000000000200000LL
#define DRUM2_FLAG      00000000000400000LL
#define DSK1_FLAG       00000000001000000LL
#define DSK2_FLAG       00000000002000000LL
#define PRT1_FLAG       00000000004000000LL
#define PRT2_FLAG       00000000010000000LL
#define PUNCH_FLAG      00000000020000000LL
#define CARD1_FLAG      00000000040000000LL
#define CARD2_FLAG      00000000100000000LL
#define SPO_FLAG        00000000200000000LL
#define PTP1_FLAG       00000000400000000LL
#define PTR1_FLAG       00000001000000000LL
#define PTR2_FLAG       00000002000000000LL
#define PTP2_FLAG       00000004000000000LL
#define DTC_FLAG        00000010000000000LL

#endif /* _B5500_H_ */
