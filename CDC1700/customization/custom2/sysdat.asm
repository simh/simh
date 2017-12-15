       NAM  SYSDAT       EXXON DEVELOPMENT SYSTEM            SUMMARY-122
*      SYSTEM DATA PROGRAM  -  MSOS 5.0
*      1700 MASS STORAGE OPERATING SYSTEM VERSION 5.0
*      SMALL SYSTEMS DIVISION, LA JOLLA, CALIFORNIA
*      COPYRIGHT CONTROL DATA CORPORATION 1976
*
*      SIMH  DEVELOPMENT SYSTEM
*
*                         PROGRAM BASE - MSOS 4.3
*
*           S Y S T E M   D A T A   P R O G R A M
*
*
*      TABLE OF CONTENTS
*
*           1. COMMUNICATION EXTERNALS
*
*           2. COMMUNICATION REGION (INCLUDING APPLICATIONS AREA)
*
*           3. INTERRUPT REGION
*
*           4. INTERRUPT MASK TABLE (MASKT)
*
*           5. EXTENDED COMMUNICATIONS REGION
*
*           6. STORAGE STACKS (INTSTK,VOLBLK, SCHSTK)
*
*           7. LOGICAL UNIT TABLES (LOG1A, LOG1, LOG2)
*
*           8. DIAGNOSTIC TABLES (DGNTAB, ALTERR)
*
*           9. STANDARD LOGICAL UNIT DEFINITIONS AND LINE 1 TABLE
*
*          10. PHYSICAL DEVICE TABLES WITH INTERRUPT RESPONSE ROUTINES
*
*          11. CORE ALLOCATION INFORMATION (CALTHD, LVLSTR, NN'S)
*
*          12. CORE PARTITION  INFORMATION (PARTBL, THDS, USE)
*
*          13. SYSTEM COMMON DECLARATION
*
*          14. MISCELLANEOUS PROGRAMS
*
*          15. MISCELLANEOUS INFORMATION
*
*          16. SYSTEM FILE INFORMATION
*
*          17. PRESET REGION
*
*          18. START OF SYSTEM DIRECTORY
*
       EJT
*           C O M M U N I C A T I O N S   E X T E R N A L S
*
*
       EXT  FNR           FIND NEXT REQUEST
       EXT  COMPRQ        COMPLETE REQUEST
       EXT  REQXT         REQUEST EXIT
       EXT  VOLR          VOLATILE RELEASE
       EXT  VOLA          VOLATILE ASSIGNMENT
       EXT  LUABS         LOGICAL UNIT ABSOLUTIZING
       EXT  SABS          STARTING ADDRESS ABSOLUTIZING
       EXT  CABS          COMPLETION ADDRESS ABSOLUTIZING
       EXT  NABS          NUMBER OF WORDS ABSOLUTIZING
       EXT  DISPXX        DISPATCHER
       EXT  MONI          MONITOR
       EXT  MSIZV4        HIGHEST CORE LOCATION USED BY SYSTEM
       EXT  IPROC         INTERNAL INTERRUPT PROCESSOR
       EXT  ALLIN         COMMON INTERRUPT HANDLER
       EJT
*           C O M M U N I C A T I O N   R E G I O N
*
       ORG  0
       RTJ  SYFAIL        GO TO COMMON SYSTEM FAILURE ROUTINE
*
LPMSK  NUM  0             LOGICAL PRODUCT MASK TABLE OF ONES
ONE    NUM  1                ONE
THREE  NUM  3                THREE
SEVEN  NUM  7                SEVEN
       NUM  $F
       NUM  $1F
       NUM  $3F
       NUM  $7F
       NUM  $FF
       NUM  $1FF
       NUM  $3FF
       NUM  $7FF
       NUM  $FFF
       NUM  $1FFF
       NUM  $3FFF
       NUM  $7FFF
*
NZERO  NUM  $FFFF         LOGICAL PRODUCT MASK TABLE OF ZEROS (NEG ZERO)
       NUM  $FFFE
       NUM  $FFFC
       NUM  $FFF8
       NUM  $FFF0
       NUM  $FFE0
       NUM  $FFC0
       NUM  $FF80
       NUM  $FF00
       NUM  $FE00
       NUM  $FC00
       NUM  $F800
       NUM  $F000
       NUM  $E000
       NUM  $C000
       NUM  $8000
       EJT
*           C O M M U N I C A T I O N   R E G I O N
*
ZERO   NUM  0                ZERO
*
ONEBIT NUM  1             ONE BIT TABLE
TWO    NUM  2                TWO
FOUR   NUM  4                FOUR
EIGHT  NUM  8                EIGHT
       NUM  $10
       NUM  $20
       NUM  $40
       NUM  $80
       NUM  $100
       NUM  $200
       NUM  $400
       NUM  $800
       NUM  $1000
       NUM  $2000
       NUM  $4000
       NUM  $8000
*
ZROBIT NUM  $FFFE         ZERO BIT TABLE
       NUM  $FFFD
       NUM  $FFFB
       NUM  $FFF7
       NUM  $FFEF
       NUM  $FFDF
       NUM  $FFBF
       NUM  $FF7F
       NUM  $FEFF
       NUM  $FDFF
       NUM  $FBFF
       NUM  $F7FF
       NUM  $EFFF
       NUM  $DFFF
       NUM  $BFFF
       NUM  $7FFF
*
FIVE   NUM  5                FIVE
SIX    NUM  6                SIX
NINE   NUM  9                NINE
TEN    NUM  10               TEN
       EJT
*           C O M M U N I C A T I O N   R E G I O N
*
*                         THIS AREA IS AVAILABLE FOR APPLICATIONS USE
*
       NUM  0             $47
       NUM  0             $48
       NUM  0             $49
       NUM  0             $4A
       NUM  0             $4B
       NUM  0             $4C
       NUM  0             $4D
       NUM  0             $4E
       NUM  0             $4F
       NUM  0             $50
       NUM  0             $51
       NUM  0             $52
       NUM  0             $53
       NUM  0             $54
       NUM  0             $55
       NUM  0             $56
       NUM  0             $57
       NUM  0             $58
       NUM  0             $59
       NUM  0             $5A
       NUM  0             $5B
       NUM  0             $5C
       NUM  0             $5D
       NUM  0             $5E
       NUM  0             $5F
       NUM  0             $60
       NUM  0             $61
       NUM  0             $62
       NUM  0             $63
       NUM  0             $64
       NUM  0             $65
       NUM  0             $66
       NUM  0             $67
       NUM  0             $68
       NUM  0             $69
       NUM  0             $6A
       NUM  0             $6B
       NUM  0             $6C
       NUM  0             $6D
       NUM  0             $6E
       NUM  0             $6F
       EJT
       SPC  4
*           C O M M U N I C A T I O N   R E G I O N
*
*                         THIS AREA IS AVAILABLE FOR APPLICATIONS USE
*
       NUM  0             $70
       NUM  0             $71
       NUM  0             $72
       NUM  0             $73
       NUM  0             $74
       NUM  0             $75
       NUM  0             $76
       NUM  0             $77
       NUM  0             $78
       NUM  0             $79
       NUM  0             $7A
       NUM  0             $7B
       NUM  0             $7C
       NUM  0             $7D
       NUM  0             $7E
       NUM  0             $7F
       NUM  0             $80
       NUM  0             $81
       NUM  0             $82
       NUM  0             $83
       NUM  0             $84
       NUM  0             $85
       NUM  0             $86
       NUM  0             $87
       NUM  0             $88
       NUM  0             $89
       NUM  0             $8A
       NUM  0             $8B
       NUM  0             $8C
       NUM  0             $8D
       NUM  0             $8E
       NUM  0             $8F
       EJT
       SPC  2
*           C O M M U N I C A T I O N   R E G I O N
*
*                         THIS AREA IS AVAILABLE FOR APPLICATIONS USE
*
       NUM  0             $90
       NUM  0             $91
       NUM  0             $92
       NUM  0             $93
       NUM  0             $94
       NUM  0             $95
       NUM  0             $96
       NUM  0             $97
       NUM  0             $98
       NUM  0             $99
       NUM  0             $9A
       NUM  0             $9B
       NUM  0             $9C
       NUM  0             $9D
       NUM  0             $9E
       NUM  0             $9F
       NUM  0             $A0
       NUM  0             $A1
       NUM  0             $A2
       NUM  0             $A3
       NUM  0             $A4
       NUM  0             $A5
       NUM  0             $A6
       NUM  0             $A7
       NUM  0             $A8
       NUM  0             $A9
       NUM  0             $AA
       NUM  0             $AB
       NUM  0             $AC
       NUM  0             $AD
       NUM  0             $AE
       NUM  0             $AF
       NUM  0             $B0
       NUM  0             $B1
       NUM  0             $B2
       EJT
*           C O M M U N I C A T I O N   R E G I O N
*
       ORG  $B3
       ADC  SCRTCH        LOGICAL UNIT OF STANDARD SCRATCH DEVICE
       ADC  SCHSTK        ADR OF TOP OF SCHEDULER STACK
AFNR   ADC  FNR           ADR OF FIND NEXT REQUEST
ACOMPR ADC  COMPRQ        ADR OF COMPLETE REQUEST
       ADC  MASKT         ADR OF MASK TABLE
       ADC  INTSTK        ADR OF TOP OF INTERRUPT STACK
       ADC  REQXT         ADR OF EXIT FOR MONITOR REQUESTS
AVOLR  ADC  VOLR          ADR OF RELEASE VOLATILE ROUTINE
AVOLA  ADC  VOLA          ADR OF ASSIGN VOLATILE ROUTINE
       ADC  LUABS         ADR OF ABSOLUTIZING ROUTINE FOR LOGICAL UNIT
       ADC  SABS          ADR OF ABSOLUTIZING ROUTINE FOR STARTING ADR
       ADC  CABS          ADR OF ABSOLUTIZING ROUTINE FOR COMPLETION ADR
       ADC  NABS          ADR OF ABSOLUTIZING ROUTINE FOR NUMBER OF WRDS
       NUM  0             MSB OF STARTING SCRATCH SECTOR   (ALWAYS ZERO)
       NUM  0             LSB OF STARTING SCRATCH SECTOR     (SET BY SI)
       ADC  LBUNIT        LOGICAL UNIT OF STANDARD LIBRARY DEVICE
       NUM  0             MSB OF PGM LIB DIRECTORY SECTOR  (ALWAYS ZERO)
       NUM  0             LSB OF PGM LIB DIRECTORY SECTOR    (SET BY SI)
*
       BZS  ($E3-$C5+1)   RESERVED FOR FTN                 (UNPROTECTED)
       NUM  0             RESERVED FOR FTN + LOAD/GO SECTOR(UNPROTECTED)
       NUM  0             RESERVED FOR FTN                 (UNPROTECTED)
*
       BSS  (1)           LENGTH OF MASS RESIDENT SYSTEM DIR.(SET BY SI)
       BSS  (1)           LENGTH OF CORE RESIDENT SYSTEM DIR.(SET BY SI)
       NUM  0             REAL TIME CLOCK COUNTER
       ADC  EXTBV4        ADDR OF EXTENDED CORE TABLE
ADISP  ADC  DISPXX        ADR OF DISPATCHER
       ADC  SLDIRY        ADR OF SYSTEM DIRECTORY
       NUM  0             TEMPORARY TOP+1 OF UNPROTECTED     (SET BY SI)
       NUM  0             TEMPORARY BOTTOM-1 OF UNPROTECTED  (SET BY SI)
       NUM  0             USED BY JOB PROCESSOR FOR LOADER RETURNS
       NUM  -1            CURRENT PRIORITY LEVEL
       ADC  VOLBLK        STARTING LOCATION OF VOLATILE STORAGE
       ADC  LPRSET        LENGTH OF PRESETS TABLE
       ADC  APRSET        STARTING LOCATION OF PRESETS TABLE
       ADC  0             ADR OF BREAKPOINT PROGRAM IN CORE(UNPROTECTED)
AMONI  ADC  MONI          ADR OF MONITOR ENTRY FOR REQUESTS
       ADC  MSIZV4        HIGHEST CORE LOCATION USED BY SYSTEM
       NUM  0             TOP+1 OF UNPROTECTED               (SET BY SI)
       NUM  0             BOTTOM-1 OF UNPROTECTED            (SET BY SI)
       ADC  IPROC         ADR OF INTERNAL INTERRUPT PROCESSOR
       ADC  STDINP        LOGICAL UNIT OF STANDARD INPUT DEVICE  (FTN 1)
       ADC  BINOUT        LOGICAL UNIT OF STANDARD BINARY DEVICE (FTN 2)
       ADC  LSTOUT        LOGICAL UNIT OF STANDARD PRINT DEVICE  (FTN 3)
       ADC  OUTCOM        LOGICAL UNIT OF OUTPUT COMMENT DEVICE  (FTN 4)
       ADC  INPCOM        LOGICAL UNIT OF INPUT  COMMENT DEVICE  (FTN 4)
       ADC  ALLIN         ADR OF COMMON INTERRUPT HANDLER
       BSS  (1)           I (MEMORY INDEX) REGISTER        (UNPROTECTED)
       EJT
       SPC  6
*           I N T E R R U P T   R E G I O N
*
*
*
LINE00 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($F8)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  15            PRIORITY LEVEL OF INTERRUPT
       ADC  IPROC         INTERRUPT RESPONSE FOR THE PROTECT/PARITY ERR.
*
LINE01 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  13            PRIORITY LEVEL OF INTERRUPT
       ADC  LIN1V4        INTERRUPT RESPONSE FOR THE LOW SPEED  I / O
*
LINE02 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  09            PRIORITY LEVEL OF INTERRUPT
       ADC  R1752         INTERRUPT RESPONSE FOR THE 1752 DRUM
*
LINE03 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  09            PRIORITY LEVEL OF INTERRUPT
       ADC  R17332        INTERRUPT RESPONSE FOR THE 1733-2/856-2/4 DISK
*
LINE04 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  10            PRIORITY LEVEL OF INTERRUPT
       ADC  R42312        INTERRUPT RESPONSE FOR THE 1742-30/120 PRINTER
*
LINE05 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  10            PRIORITY LEVEL OF INTERRUPT
       ADC  R17432        INTERRUPT RESPONSE FOR THE 1743-2 COMM. CONT.
*
LINE06 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  0             PRIORITY LEVEL OF INTERRUPT
       ADC  INVINT        INTERRUPT RESPONSE FOR THE INVALID INTERRUPTS
       SPC  1
*
LINE07 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  10            PRIORITY LEVEL OF INTERRUPT
       ADC  R17323        INTERRUPT RESPONSE FOR THE 1732-3/616 MAG TAPE
*
LINE08 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  0             PRIORITY LEVEL OF INTERRUPT
       ADC  INVINT        INTERRUPT RESPONSE FOR THE INVALID INTERRUPTS
       SPC  1
*
LINE09 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  0             PRIORITY LEVEL OF INTERRUPT
       ADC  INVINT        INTERRUPT RESPONSE FOR THE INVALID INTERRUPTS
       SPC  1
*
LINE10 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  14            PRIORITY LEVEL OF INTERRUPT
       ADC  R1728         INTERRUPT RESPONSE FOR THE 1728-430 READ/PNCH
*
LINE11 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  0             PRIORITY LEVEL OF INTERRUPT
       ADC  INVINT        INTERRUPT RESPONSE FOR THE INVALID INTERRUPTS
       SPC  1
*
LINE12 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  0             PRIORITY LEVEL OF INTERRUPT
       ADC  INVINT        INTERRUPT RESPONSE FOR THE INVALID INTERRUPTS
       SPC  1
*
LINE13 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  0             PRIORITY LEVEL OF INTERRUPT
       ADC  INVINT        INTERRUPT RESPONSE FOR THE INVALID INTERRUPTS
       SPC  1
*
LINE14 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  0             PRIORITY LEVEL OF INTERRUPT
       ADC  INVINT        INTERRUPT RESPONSE FOR THE INVALID INTERRUPTS
       SPC  1
*
LINE15 NUM  0             INTERRUPT LINE ENTRY
       RTJ- ($FE)         GO TO INTERRUPT HANDLER ROUTINE
       NUM  0             PRIORITY LEVEL OF INTERRUPT
       ADC  INVINT        INTERRUPT RESPONSE FOR THE INVALID INTERRUPTS
       SPC  1
       EJT
*      C O R E   R E S I D E N T   D E B U G   E N T R I E S
       SPC  2
       ORG  $140
       SPC  1
       EXT  COUTV4
       EXT  COBOP
       SPC  2
       JMP+ COUTV4        OFF-LINE CORE DUMP
       SPC  4
       JMP+ COBOP         SYSTEM CHECKOUT BOOTSTRAP
       EJT
*           I N T E R R U P T   M A S K   T A B L E
*
*
        ENT  MASKT         INTERRUPT MASKS INDEXED BY PRIORITY LEVEL
*
*
*            <----------------------------- INTERRUPT LINE NUMBER
*              15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
*            ****************************************************
*       P -1 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*       R  0 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*       I  1 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*       O  2 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*       R  3 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*       I  4 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*       T  5 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*       Y  6 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*          7 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*       L  8 *  0  0  0  0  0  1  0  0  1  0  1  1  1  1  1  1  *
*       E  9 *  0  0  0  0  0  1  0  0  1  0  1  1  0  0  1  1  *
*       V 10 *  0  0  0  0  0  1  0  0  0  0  0  0  0  0  1  1  *
*       E 11 *  0  0  0  0  0  1  0  0  0  0  0  0  0  0  1  1  *
*       L 12 *  0  0  0  0  0  1  0  0  0  0  0  0  0  0  1  1  *
*       . 13 *  0  0  0  0  0  1  0  0  0  0  0  0  0  0  0  1  *
*       . 14 *  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  1  *
*       V 15 *  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  *
*            ****************************************************
*
*
       NUM  $04BF         PRIORITY LEVEL -1
MASKT  NUM  $04BF         PRIORITY LEVEL 00
       NUM  $04BF         PRIORITY LEVEL 01
       NUM  $04BF         PRIORITY LEVEL 02
       NUM  $04BF         PRIORITY LEVEL 03
       NUM  $04BF         PRIORITY LEVEL 04
       NUM  $04BF         PRIORITY LEVEL 05
       NUM  $04BF         PRIORITY LEVEL 06
       NUM  $04BF         PRIORITY LEVEL 07
       NUM  $04BF         PRIORITY LEVEL 08
       NUM  $04B3         PRIORITY LEVEL 09
       NUM  $0403         PRIORITY LEVEL 10
       NUM  $0403         PRIORITY LEVEL 11
       NUM  $0403         PRIORITY LEVEL 12
       NUM  $0401         PRIORITY LEVEL 13
       NUM  $0001         PRIORITY LEVEL 14
       NUM  $0000         PRIORITY LEVEL 15
       EJT
*           E X T E N D E D  C O M M U N I C A T I O N S  R E G I O N
*
*                         REFERENCED THRU LOCATION $E9
       SPC  3
       ENT  MAXSEC
       ENT  MPFLAG
       ENT  MIINP
       EXT  JFILV4
       EXT  RCTV
       EXT  END0V4
       EXT  DATBAS
       EXT  SECTOR
       EQU  CSYLST(9)
       EQU  CSYINP(10)
       EQU  CSYPUN(11)
       EQU  SECT1(0)
       EQU  SECT3(0)
       EQU  SECT4($5BFA)
       SPC  3
EXTBV4 ADC  0             00   MODE SWITCH   32K=0   65K=1
       ADC  CSYINP        01   STANDARD COSY INPUT  LU NUMBER
       ADC  CSYPUN        02   STANDARD COSY OUTPUT LU NUMBER
       ADC  CSYLST        03   STANDARD COSY LIST   LU NUMBER
       ADC  0             04   FIRST SECTOR LSB OF SYSTEM CORE IMAGE
       ADC  0             05   FIRST SECTOR LSB OF  S.  A.  T.
       ADC  0             06   FIRST SECTOR LSB OF CREP  TABLE
       ADC  0             07   FIRST SECTOR LSB OF CREP1 TABLE
       ADC  JFILV4        08   FIRST SECTOR LSB OF JOB FILE DIRECTORY
       ADC  RCTV          09   ADDRESS OF RCTV TABLE IN THE MONITOR
       ADC  0             10   UNPROTECTED CORE FLAG  0=PART0 / 1=PART1
       ADC  0             11   UNPROTECTED SWAP ALLOWED 0=YES / 1=NO
       ADC  AYERTO        12   ADDRESS LOCATION CONTAINING THE YEAR
       ADC  AMONTO        13   ADDRESS LOCATION CONTAINING THE MONTH
       ADC  ADAYTO        14   ADDRESS LOCATION CONTAINING THE DAY
       ADC  END0V4        15   LAST ADDRESS OF PART 0 CORE
       ADC  0             16   FIRST ADDRESS OF BLANK (SYSTEM) COMMON
       ADC  DATBAS        17   FIRST ADDRESS OF LABELED COMMON
       ADC  0             18   COSY DRIVER CURRENT PHYSTAB ADDRESS
       ADC  0             19   JOB TABLE INITIALIZATION FLAG
       ADC  0             20   MASS MEMORY LOCATION OF ENGINEERING FILE
       ADC  SECT1         21   MSB OF MAXIMUM SCRATCH SECTOR
MAXSEC ADC  SECTOR        22   LSB OF MAXIMUM SCRATCH SECTOR
       ADC  SECT3         23   MSB OF MAXIMUM LIBRARY SECTOR
       ADC  SECT4         24   LSB OF MAXIMUM LIBRARY SECTOR
       ADC  0             25   LAST ADDRESS OF LABELED COMMON
       ADC  0             26   UNUSED
MPFLAG ADC  0             27   ZERO IF NOT AN MP SYSTEM
       ADC  LOG1A         28   ADDRESS OF LOG1A TABLE
MIINP  BZS  MIINP(40)     MANUAL INPUT BUFFER
       EJT
*           S Y S T E M   I D E N T I F I C A T I O N
*
       SPC  1
       ENT  SYSID
       EXT  SYSMON        MONTH SYSTEM WAS BUILT
       EXT  SYSDAY        DAY   SYSTEM WAS BUILT
       EXT  SYSYER        YEAR  SYSTEM WAS BUILT
       SPC  4
SYSID  ALF 16, SIMH  DEVELOPMENT SYSTEM
       ADC SYSMON
       ADC SYSDAY
       ADC SYSYER
       SPC  4
*      COMMON SYSTEM FAILURE ROUTINE
       SPC  2
       ENT  SYFAIL
       SPC  1
SYFAIL NOP  0
       IIN  0             INHIBIT INTERRUPTS
       STA* SAVEA         SAVE A
       STQ* SAVEQ         SAVE Q
       TRM  A             MOVE M TO A
       STA* SAVEM         SAVE M
       LDA* SYFAIL        PICK UP ADDRESS OF CALLER
       INA  -2            CORRECT IT FOR 2 WORD RTJ
       STA* SYFAIL        STORE IT BACK
       NUM  $18FF         HANG
       SPC  2
SAVEA  NUM  0
SAVEQ  NUM  0
SAVEM  NUM  0
       EJT
*      C O N T R O L   P O I N T  /  B O U N D S   R E G I S T E R
*                         P A R A M E T E R S
*
       ENT  UBPROT
       ENT  LBPROT
       ENT  UPBDTB
       ENT  LOBDTB
       ENT  TSCNAC
       ENT  TSCNMI
       ENT  SIM200
       ENT  CCP
       ENT  CPSET
       SPC
       EQU  UBPROT($7FFF),LBPROT($7FFF),UPBDTB($7FFF),LOBDTB($7FFF)
       EQU  TSCNAC($7FFF),TSCNMI($7FFF),SIM200($7FFF)
       EQU  CCP($7FFF)
       SPC  2
CPSET  NUM  0
       JMP* (CPSET)
       EJT
*           S T O R A G E   S T A C K S
*
*
NUMPRI EQU NUMPRI(16)     NUMBER OF SYSTEM PRIORITY LEVELS
EXTVOL EQU EXTVOL(00)     AMOUNT OF EXTRA VOLATILE STORAGE
*
NFTNLV EQU NFTNLV(3)      NUMBER OF REENTRANT FORTRAN LEVELS
NEDLVL EQU NEDLVL(3)      NUMBER OF REENTRANT ENCODE/DECODE LEVELS
       SPC  3
*
*                  I N T E R R U P T   S T A C K
*
       ENT  INTSTK        CONTENTS,  1 = Q-REGISTER
*                                    2 = A-REGISTER
*                                    3 = I-REGISTER
*                                    4 = P-REGISTER
*                                    5 = PRIORITY LEVEL AND OVERFLOW
*                                        INDICATOR (BIT 15)
*
INTSTK BZS  INTSTK(5*NUMPRI)
       SPC  3
*
*                  V O L A T I L E   B L O C K   S T A C K
*
       ENT  VOLBLK        CONTENTS,  1 = Q-REGISTER
       ENT  VOLEND                   2 = A-REGISTER
*                                    3 = I-REGISTER
*                                    4 = USER ASSIGNMENTS
*                                    .
*                                    N = USER ASSIGNMENTS
*
VOLBLK BZS  VOLBLK(18*NUMPRI+98*NFTNLV+57*NEDLVL+EXTVOL+1)
VOLEND EQU  VOLEND(*)     END OF VOLATILE
       SPC  3
*
*                  S C H E D U L E R / T I M E R   S T A C K
*
       ENT  SCHSTK        CONTENTS,  1 = SCHEDULER CALL
       ENT  SCHLNG                   2 = STARTING ADDRESS
*                                    3 = THREAD TO NEXT CALL
SCHSTK EQU  SCHSTK(*)                4 = Q-REGISTER CONTENTS
       SPC  1
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  001
       EJT
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  002
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  003
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  004
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  005
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  006
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  007
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  008
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  009
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  010
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  011
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  012
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  013
       EJT
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  014
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  015
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  016
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  017
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  018
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  019
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  020
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  021
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  022
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  023
       ADC  0,0,*+2,0     SCHEDULER STACK ENTRY  024
       ADC  0,0,(-0),0    SCHEDULER STACK ENTRY  025
SCHLNG EQU  SCHLNG(*-SCHSTK)  SCHEDULER STACK LENGTH
       EJT
*           L O G I C A L   U N I T   T A B L E S   ( L O G 1 A )
*
*
       ENT  LOG1A     PHYSICAL DEVICES ADDRESSES BY LOGICAL UNIT
       ENT  NUMLU
       SPC  1
LOG1A  ADC  NUMLU         NUMBER OF LOGICAL UNITS
       ADC  PCORE         1    CORE ALLOCATOR
       ADC  PDUMMY        2    DUMMY LOGICAL UNIT
       ADC  PDUMMY        3    DUMMY LOGICAL UNIT
       ADC  P1711         4    1711 TELETYPE, 713-10 CRT
       ADC  PCOSY1        5    COSY DRIVER, FIRST UNIT
       ADC  P73230        6    1732-3 616-73/93/95 MAG TAPE UNIT 0
       ADC  PSUDO0        7    PSEUDO TAPE, UNIT 0
       ADC  P73320        8    1733-2 856-2/4 DISK, UNIT 0
       ADC  P42312        9    1742-30/120 LINE PRINTER
X73230 ADC  P73230        10   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 0
       ADC  P1728         11   1728-430 CARD PUNCH
FTN742 ADC  P42312        12   1742-30/120 FORTRAN LINE PRINTER
       ADC  P73321        13   1733-2 856-2/4 DISK, UNIT 1
       ADC  P73322        14   1733-2 856-2/4 DISK, UNIT 2
       ADC  P73323        15   1733-2 856-2/4 DISK, UNIT 3
       ADC  P73231        16   1732-3 616-73/93/95 MAG TAPE UNIT 1
       ADC  P73232        17   1732-3 616-73/93/95 MAG TAPE UNIT 2
       ADC  P73233        18   1732-3 616-73/93/95 MAG TAPE UNIT 3
       ADC  PSUDO1        19   PSEUDO TAPE, UNIT 1
       ADC  PSUDO2        20   PSEUDO TAPE, UNIT 2
       ADC  PSUDO3        21   PSEUDO TAPE, UNIT 3
       ADC  PSDSK0        22   PSEUDO DISK, UNIT 0
       ADC  PSDSK1        23   PSEUDO DISK, UNIT 1
       ADC  PSDSK2        24   PSEUDO DISK, UNIT 2
       ADC  PSDSK3        25   PSEUDO DISK, UNIT 3
       ADC  P1752         26   1752 DRUM
       ADC  PCOSY2        27   COSY DRIVER, SECOND UNIT
       ADC  P1728         28   1728-430 CARD READER
X73231 ADC  P73231        29   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 1
L74300 ADC  P74300        30   1743-2 COMMUNICATIONS UNIT  0
L74301 ADC  P74301        31   1743-2 COMMUNICATIONS UNIT  1
L74302 ADC  P74302        32   1743-2 COMMUNICATIONS UNIT  2
L74303 ADC  P74303        33   1743-2 COMMUNICATIONS UNIT  3
L74304 ADC  P74304        34   1743-2 COMMUNICATIONS UNIT  4
L74305 ADC  P74305        35   1743-2 COMMUNICATIONS UNIT  5
X73232 ADC  P73232        36   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 2
X73233 ADC  P73233        37   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 3
X42312 ADC  P42312        38   DIAGNOSTIC 1742-30/120 LINE PRINTER
X1728  ADC  P1728         39   DIAGNOSTIC 1728-430 READER / PUNCH
X74300 ADC  P74300        40   DIAGNOSTIC 1743-2 COMM. , UNIT 0
X74301 ADC  P74301        41   DIAGNOSTIC 1743-2 COMM. , UNIT 1
X74302 ADC  P74302        42   DIAGNOSTIC 1743-2 COMM. , UNIT 2
X74303 ADC  P74303        43   DIAGNOSTIC 1743-2 COMM. , UNIT 3
X74304 ADC  P74304        44   DIAGNOSTIC 1743-2 COMM. , UNIT 4
X74305 ADC  P74305        45   DIAGNOSTIC 1743-2 COMM. , UNIT 5
X1711  ADC  P1711         46   DIAGNOSTIC 1711 TELETYPE, 713-10 CRT
NUMLU  EQU  NUMLU(*-LOG1A-1)
       EJT
*           L O G I C A L   U N I T   T A B L E S   ( L O G 1 )
*
*
       ENT  LOG1      LOGICAL UNIT INFORMATION BY LOGICAL UNIT
*                         BIT 14 = 1, IMPLIES LU SHARES DEVICE
*                         BIT 13 = 1, IMPLIES LU IS MARKED DOWN
*                         BITS 0 - 11 IS ALTERNATE LOGICAL UNIT
*                                        ALTERNATE = 0, IMPLIES NONE
S      EQU  S($4000)      SHARED BIT
       SPC  1
LOG1   ADC  NUMLU         NUMBER OF LOGICAL UNITS
       ADC  0             1    CORE ALLOCATOR
       ADC  0+S           2    DUMMY LOGICAL UNIT
       ADC  0+S           3    DUMMY LOGICAL UNIT
       ADC  2+S           4    1711 TELETYPE, 713-10 CRT
       ADC  0             5    COSY DRIVER, FIRST UNIT
       ADC  0+S           6    1732-3 616-73/93/95 MAG TAPE UNIT 0
       ADC  0             7    PSEUDO TAPE, UNIT 0
       ADC  0             8    1733-2 856-2/4 DISK, UNIT 0
       ADC  0+S           9    1742-30/120 LINE PRINTER
       ADC  0+S           10   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 0
       ADC  0+S           11   1728-430 CARD PUNCH
       ADC  0+S           12   1742-30/120 FORTRAN LINE PRINTER
       ADC  0             13   1733-2 856-2/4 DISK, UNIT 1
       ADC  0             14   1733-2 856-2/4 DISK, UNIT 2
       ADC  0             15   1733-2 856-2/4 DISK, UNIT 3
       ADC  0+S           16   1732-3 616-73/93/95 MAG TAPE UNIT 1
       ADC  0+S           17   1732-3 616-73/93/95 MAG TAPE UNIT 2
       ADC  0+S           18   1732-3 616-73/93/95 MAG TAPE UNIT 3
       ADC  0             19   PSEUDO TAPE, UNIT 1
       ADC  0             20   PSEUDO TAPE, UNIT 2
       ADC  0             21   PSEUDO TAPE, UNIT 3
       ADC  0             22   PSEUDO DISK, UNIT 0
       ADC  0             23   PSEUDO DISK, UNIT 1
       ADC  0             24   PSEUDO DISK, UNIT 2
       ADC  0             25   PSEUDO DISK, UNIT 3
       ADC  0             26   1752 DRUM
       ADC  0             27   COSY DRIVER, SECOND UNIT
       ADC  0+S           28   1728-430 CARD READER
       ADC  0+S           29   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 1
       ADC  0+S           30   1743-2 COMMUNICATIONS UNIT  0
       ADC  0+S           31   1743-2 COMMUNICATIONS UNIT  1
       ADC  0+S           32   1743-2 COMMUNICATIONS UNIT  2
       ADC  0+S           33   1743-2 COMMUNICATIONS UNIT  3
       ADC  0+S           34   1743-2 COMMUNICATIONS UNIT  4
       ADC  0+S           35   1743-2 COMMUNICATIONS UNIT  5
       ADC  0+S           36   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 2
       ADC  0+S           37   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 3
       ADC  0+S           38   DIAGNOSTIC 1742-30/120 LINE PRINTER
       ADC  0+S           39   DIAGNOSTIC 1728-430 READER / PUNCH
       ADC  0+S           40   DIAGNOSTIC 1743-2 COMM. , UNIT 0
       ADC  0+S           41   DIAGNOSTIC 1743-2 COMM. , UNIT 1
       ADC  0+S           42   DIAGNOSTIC 1743-2 COMM. , UNIT 2
       ADC  0+S           43   DIAGNOSTIC 1743-2 COMM. , UNIT 3
       ADC  0+S           44   DIAGNOSTIC 1743-2 COMM. , UNIT 4
       ADC  0+S           45   DIAGNOSTIC 1743-2 COMM. , UNIT 5
       ADC  0+S           46   DIAGNOSTIC 1711 TELETYPE, 713-10 CRT
       EJT
*           L O G I C A L   U N I T   T A B L E S   ( L O G 2 )
*
*
       ENT  LOG2      TOP OF I/O THREAD ADDRESSES BY LOGICAL UNIT
       SPC  1
LOG2   ADC  NUMLU         NUMBER OF LOGICAL UNITS
       NUM  $FFFF         1    CORE ALLOCATOR
       NUM  $FFFF         2    DUMMY LOGICAL UNIT
       NUM  $FFFF         3    DUMMY LOGICAL UNIT
       NUM  $FFFF         4    1711 TELETYPE, 713-10 CRT
       NUM  $FFFF         5    COSY DRIVER, FIRST UNIT
       NUM  $FFFF         6    1732-3 616-73/93/95 MAG TAPE UNIT 0
       NUM  $FFFF         7    PSEUDO TAPE, UNIT 0
       NUM  $FFFF         8    1733-2 856-2/4 DISK, UNIT 0
       NUM  $FFFF         9    1742-30/120 LINE PRINTER
       NUM  $FFFF         10   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 0
       NUM  $FFFF         11   1728-430 CARD PUNCH
       NUM  $FFFF         12   1742-30/120 FORTRAN LINE PRINTER
       NUM  $FFFF         13   1733-2 856-2/4 DISK, UNIT 1
       NUM  $FFFF         14   1733-2 856-2/4 DISK, UNIT 2
       NUM  $FFFF         15   1733-2 856-2/4 DISK, UNIT 3
       NUM  $FFFF         16   1732-3 616-73/93/95 MAG TAPE UNIT 1
       NUM  $FFFF         17   1732-3 616-73/93/95 MAG TAPE UNIT 2
       NUM  $FFFF         18   1732-3 616-73/93/95 MAG TAPE UNIT 3
       NUM  $FFFF         19   PSEUDO TAPE, UNIT 1
       NUM  $FFFF         20   PSEUDO TAPE, UNIT 2
       NUM  $FFFF         21   PSEUDO TAPE, UNIT 3
       NUM  $FFFF         22   PSEUDO DISK, UNIT 0
       NUM  $FFFF         23   PSEUDO DISK, UNIT 1
       NUM  $FFFF         24   PSEUDO DISK, UNIT 2
       NUM  $FFFF         25   PSEUDO DISK, UNIT 3
       NUM  $FFFF         26   1752 DRUM
       NUM  $FFFF         27   COSY DRIVER, SECOND UNIT
       NUM  $FFFF         28   1728-430 CARD READER
       NUM  $FFFF         29   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 1
       NUM  $FFFF         30   1743-2 COMMUNICATIONS UNIT  0
       NUM  $FFFF         31   1743-2 COMMUNICATIONS UNIT  1
       NUM  $FFFF         32   1743-2 COMMUNICATIONS UNIT  2
       NUM  $FFFF         33   1743-2 COMMUNICATIONS UNIT  3
       NUM  $FFFF         34   1743-2 COMMUNICATIONS UNIT  4
       NUM  $FFFF         35   1743-2 COMMUNICATIONS UNIT  5
       NUM  $FFFF         36   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 2
       NUM  $FFFF         37   DIAGNOSTIC 1732-3 616 MAG TAPE, UNIT 3
       NUM  $FFFF         38   DIAGNOSTIC 1742-30/120 LINE PRINTER
       NUM  $FFFF         39   DIAGNOSTIC 1728-430 READER / PUNCH
       NUM  $FFFF         40   DIAGNOSTIC 1743-2 COMM. , UNIT 0
       NUM  $FFFF         41   DIAGNOSTIC 1743-2 COMM. , UNIT 1
       NUM  $FFFF         42   DIAGNOSTIC 1743-2 COMM. , UNIT 2
       NUM  $FFFF         43   DIAGNOSTIC 1743-2 COMM. , UNIT 3
       NUM  $FFFF         44   DIAGNOSTIC 1743-2 COMM. , UNIT 4
       NUM  $FFFF         45   DIAGNOSTIC 1743-2 COMM. , UNIT 5
       NUM  $FFFF         46   DIAGNOSTIC 1711 TELETYPE, 713-10 CRT
       EJT
*           D I A G N O S T I C   T A B L E S
*
*
       ENT  ALTERR    ALTERNATE DEVICE ERROR TABLE
       SPC  1
ALTERR ADC  NUMLU         ERROR TABLE SIZE
       BZS  (NUMLU)       SPACE FOR MAXIMUM SIMULTANEOUS FAILURES
       SPC  3
       ENT  DGNTAB    DIAGNOSTIC TIMER TABLE
       SPC  1
DGNTAB EQU  DGNTAB(*)     START OF TABLE
       ADC  PCORE         1    CORE ALLOCATOR
       ADC  P1711         4    1711 TELETYPE, 713-10 CRT
       ADC  P73230        6    1732-3 616-73/93/95 MAG TAPE UNIT 0
       ADC  P73320        8    1733-2 856-2/4 DISK, UNIT 0
       ADC  P42312        9    1742-30/120 LINE PRINTER
       ADC  P73321        13   1733-2 856-2/4 DISK, UNIT 1
       ADC  P73322        14   1733-2 856-2/4 DISK, UNIT 2
       ADC  P73323        15   1733-2 856-2/4 DISK, UNIT 3
       ADC  P73231        16   1732-3 616-73/93/95 MAG TAPE UNIT 1
       ADC  P73232        17   1732-3 616-73/93/95 MAG TAPE UNIT 2
       ADC  P73233        18   1732-3 616-73/93/95 MAG TAPE UNIT 3
       ADC  P1752         26   1752 DRUM
       ADC  P1728         28   1728-430 CARD READER
       ADC  P74300        30   1743-2 COMMUNICATIONS UNIT  0
       ADC  P74301        31   1743-2 COMMUNICATIONS UNIT  1
       ADC  P74302        32   1743-2 COMMUNICATIONS UNIT  2
       ADC  P74303        33   1743-2 COMMUNICATIONS UNIT  3
       ADC  P74304        34   1743-2 COMMUNICATIONS UNIT  4
       ADC  P74305        35   1743-2 COMMUNICATIONS UNIT  5
       NUM  $FFFF         END OF TABLE
       EJT
       SPC  4
*           S T A N D A R D   L O G I C A L   U N I T S
*
       ENT DUMALT
*
DUMALT EQU DUMALT(2)      STANDARD DUMMY ALTERNATE
INPCOM EQU INPCOM(4)      STANDARD INPUT COMMENT
OUTCOM EQU OUTCOM(4)      STANDARD OUTPUT COMMENT
LBUNIT EQU LBUNIT(8)      STANDARD LIBRARY UNIT
SCRTCH EQU SCRTCH(8)      STANDARD SCRATCH UNIT
LSTOUT EQU LSTOUT(9)      STANDARD LIST OUTPUT
STDINP EQU STDINP(10)     STANDARD INPUT
BINOUT EQU BINOUT(11)     STANDARD BINARY OUTPUT
       EJT
*           L I N E   O N E   T A B L E
*
       SPC  1
*
       ENT  LIN1V4        LINE 1 INTERRUPT ENTRY
       SPC  1
LIN1V4 LDQ* LN1TV4        PLACE THE PDT ADDRESS IN Q
       LDA- 2,Q
       STA- I
       JMP- (I)           TRANSFER CONTROL TO THE DRIVER CONTINUATOR
       SPC  3
       ENT  INVINT
       SPC  1
INVINT JMP- (ADISP)
*
       SPC  4
       EQU  LN1TV4(*)     START OF TABLE
       ADC  P1711             1711 TELETYPE, 713-10 CRT
       NUM  $FFFF         END OF TABLE
       EJT
*           P H Y S I C A L   D E V I C E   T A B L E S
*
*
*           THE FOLLOWING SECTION CONTAINS THE PHYSICAL DEVICE TABLES
*           AND INTERRUPT RESPONSE ROUTINES  FOR EACH LOGICAL UNIT IN
*           THE SYSTEM.
*
*           LISTED BELOW ARE THE MANDATORY ENTRIES FOR ALL PHYSICAL
*           DEVICE TABLES.      ADDITIONAL ENTRIES REQUIRED BY EACH
*           DRIVER MAY BE ADDED AFTER THE LAST ENTRY INDICATED.
       SPC  3
PHYSTB EQU  PHYSTB(*)
       EQU  ELVL(0)       00  SCHEDULER CALL WITH DRIVER LEVEL
       EQU  EDIN(1)       01  DRIVER INITIATOR ENTRY
       EQU  EDCN(2)       02  DRIVER CONTINUATOR ENTRY
       EQU  EDPGM(3)      03  DRIVER DIAGNOSTIC ENTRY
       EQU  EDCLK(4)      04  DIAGNOSTIC CLOCK
       EQU  ELU(5)        05  LOGICAL UNIT
       EQU  EPTR(6)       06  PARAMETER LOCATION
       EQU  EWES(7)       07  CONVERTOR, EQUIPMENT, STATION
       EQU  EREQST(8)     08  REQUEST STATUS
       EQU  ESTAT1(9)     09  DRIVER STATUS
       EQU  ECCOR(10)     10  CURRENT LOCATION
       EQU  ELSTWD(11)    11  LAST LOCATION PLUS ONE
       EQU  ESTAT2(12)    12  DEVICE STATUS
       EQU  MASLGN(13)    13  DRIVER LENGTH (IF MASS MEMORY)
       EQU  MASSEC(14)    14  NAME ASSOCIATED WITH SECTOR NUMBER
       EQU  RETURN(15)    15  RESERVED FOR FNR AND CMR
       SPC  3
*      LINK UNSELECTED PHYSICAL DEVICE TABLES
       SPC  1
       ENT  P18ECM
       ENT  P18PGA
       ENT  P18ADD
       ENT  P18MXP
P18ECM EQU  P18ECM($7FFF)
P18PGA EQU  P18PGA($7FFF)
P18ADD EQU  P18ADD($7FFF)
P18MXP EQU  P18MXP($7FFF)
       EJT
*           C O R E   A L L O C A T O R
*
       SPC  1
       ENT  PCORE
       EXT  ICORE,ECORE
       EQU  SWAPT(0)      SWAP TIME
       SPC  1
PCORE  ADC  $5207         00  SCHEDULER CALL
       ADC  ICORE         01  INITIATOR ADDRESS
       ADC  0             02  CONTINUATOR ADDRESS   - NOT USED
       ADC  ECORE         03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $00D6         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       VFD  X16/SWAPT-1   13  TIME BETWEEN SWAPS (NONE IF NEGATIVE)
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       EJT
*           D U M M Y   L O G I C A L   U N I T
*
       SPC  1
       EXT  IDUMMY,CDUMMY,EDUMMY
       SPC  1
PDUMMY ADC  $520A         00  SCHEDULER CALL
       ADC  IDUMMY        01  INITIATOR ADDRESS
       ADC  CDUMMY        02  CONTINUATOR ADDRESS
       ADC  EDUMMY        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK      - NOT USED
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $01F6         08  REQUEST STATUS
       NUM  $8000         09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       EJT
*           1 7 1 1   T E L E T Y P E ,   7 1 3 - 1 0   C R T
*
       SPC  1
       EXT  I1711,C1711,E1711
       EQU  T713(04*$10)  TYPE CODE - 713-10 CRT
       EQU  T1711(00*$10) TYPE CODE - 1711 TELETYPE
       EQU  U1711(X1711-LOG1A)
       SPC  1
P1711  ADC  $520D         00  SCHEDULER CALL
       ADC  I1711         01  INITIATOR ADDRESS
       ADC  C1711         02  CONTINUATOR ADDRESS
       ADC  E1711         03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0091         07  CONVERTER, EQUIPMENT, STATION
       ADC  $3006+T1711   08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  ERROR CODE AND STARTING LOCATION
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  0             16  DRIVER FLAGS
       NUM  1             17  HARDWARE PARITY CHECK FLAG
       ADC  U1711         18  DIAGNOSTIC LU
       EJT
*           C O S Y   D R I V E R
*
       SPC  1
       EQU  MCOSY1(1)
*
       IFA  MCOSY1,EQ,0   CORE RESIDENT DRIVER
       EXT  ICOSY
       EQU  LCOSY(0)
       EQU  SCOSY($7FFF)
       EIF
*
       IFA  MCOSY1,EQ,1   MASS RESIDENT DRIVER
       EXT  MASDRV
ICOSY  JMP+ MASDRV        INITIATE DRIVER
       EXT  LCOSY
       EXT  SCOSY
       EIF
*
       EJT
*           C O S Y   D R I V E R ,   F I R S T   U N I T
*
       SPC  1
PCOSY1 ADC  $5208         00  SCHEDULER CALL
       ADC  ICOSY         01  INITIATOR ADDRESS
       ADC  0             02  CONTINUATOR ADDRESS   - NOT USED
       ADC  0             03  TIMEOUT ERROR ADDRESS - NOT USED
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $08B6         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       ADC  LCOSY         13  DRIVER LENGTH IF MASS MEMORY
       ADC  SCOSY         14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       ADC  PCOSY2        16  PHYSTB THREAD
       NUM  0             17  SEQUENCE NUMBER
       NUM  0             18  ID
       NUM  0             19  ID-1
       NUM  0             20  ID-2
       NUM  0             21  NUMBER OF WORDS REQUESTED
       NUM  $5555         22  HOL-CHARACTER POINTER
       NUM  $5555         23  COSY-CHARACTER POINTER
       NUM  0             24  R/W FLAG
       NUM  1             25  PON5F
       NUM  0             26  FSTCHR
       NUM  0             27  ENDDCK
       ADC  INPBFA        28  COSY BUFFER LOCATION
       ADC  INPBFA        29  NEXT COSY WORD
       NUM  0             30  HOL-BUFFER POINTER
       NUM  1             31  INITIAL CALL FLAG
       NUM  0             32  SEQUENCE FLAG  NONZERO=NO SEQUENCE NUMBER
*
       BZS  INPBFA(192)   33  COSY BUFFER
*                        224  COSY BUFFER
       EJT
*           C O S Y   D R I V E R ,   S E C O N D   U N I T
*
       SPC  1
PCOSY2 ADC  $5208         00  SCHEDULER CALL
       ADC  ICOSY         01  INITIATOR ADDRESS
       ADC  0             02  CONTINUATOR ADDRESS   - NOT USED
       ADC  0             03  TIMEOUT ERROR ADDRESS - NOT USED
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $08B6         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       ADC  LCOSY         13  DRIVER LENGTH IF MASS MEMORY
       ADC  SCOSY         14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       ADC  PCOSY1        16  PHYSTB THREAD
       NUM  0             17  SEQUENCE NUMBER
       NUM  0             18  ID
       NUM  0             19  ID-1
       NUM  0             20  ID-2
       NUM  0             21  NUMBER OF WORDS REQUESTED
       NUM  $5555         22  HOL-CHARACTER POINTER
       NUM  $5555         23  COSY-CHARACTER POINTER
       NUM  0             24  R/W FLAG
       NUM  1             25  PON5F
       NUM  0             26  FSTCHR
       NUM  0             27  ENDDCK
       ADC  INPBFB        28  COSY BUFFER LOCATION
       ADC  INPBFB        29  NEXT COSY WORD
       NUM  0             30  HOL-BUFFER POINTER
       NUM  1             31  INITIAL CALL FLAG
       NUM  0             32  SEQUENCE FLAG  NONZERO=NO SEQUENCE NUMBER
*
       BZS  INPBFB(192)   33  COSY BUFFER
*                        224  COSY BUFFER
       EJT
*           6 1 6 - 7 2 / 9 2 / 9 5   M A G   T A P E
*
       SPC  1
       EQU  T6167(53*$10) TYPE CODE - 616-72    MAG TAPE
       EQU  T6169(54*$10) TYPE CODE - 616-92-95 MAG TAPE
       EQU  PHSREC(192)   MAX. PHYSICAL RECORD SIZE FOR  7  TRACK
       SPC  2
BF1F33 BZS  BF1F33(PHSREC*4/3+2)     PACK/UNPACK BUFFER (7 TRACK)
       SPC  1
       EQU  M17323(1)
*
       IFA  M17323,EQ,0   CORE RESIDENT DRIVER
       EXT  I17323
       EXT  C17323
       EXT  E17323
       EQU  L17323(0)
       EQU  S17323($7FFF)
       EIF
*
       IFA  M17323,EQ,1   MASS RESIDENT DRIVER
       EXT  MASDRV
       EXT  MASCON
       EXT  MASERR
I17323 JMP+ MASDRV        INITIATE DRIVER
C17323 JMP+ MASCON        INTERRUPT RESPONSE
E17323 JMP+ MASERR        TIMEOUT ERROR
       EXT  L17323
       EXT  S17323
       EIF
*
       SPC  2
R17323 LDQ  =XP73230      INTERRUPT RESPONSE FOR 616-72-92-95 MAG TAPE
       JMP* (P73230+2)
       EJT
*           6 1 6 - 7 2 / 9 2 / 9 5   M A G   T A P E ,   U N I T   0
*
       EQU  U73230(X73230-LOG1A)
       SPC  1
P73230 ADC  $520A         00  SCHEDULER CALL
       ADC  I17323        01  INITIATOR ADDRESS
       ADC  C17323        02  CONTINUATOR ADDRESS
       ADC  E17323        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0381         07  CONVERTER, EQUIPMENT, STATION
       ADC  $0806+T6169   08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       ADC  L17323        13  DRIVER LENGTH IF MASS MEMORY
       ADC  S17323        14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  $0448         16  REC. OPT., UNIT, FUNCTION, DENSITY CONTROL
       ADC  U73230        17  DIAGNOSTIC LU
       ADC  0             18  ERROR CODE
       NUM  0             19  RECOVERY RETURN ADDRESS
       NUM  0             20  RECORD CHECKSUM
       NUM  $D554         21  FUNCTION DIRECTORY BITWORD
       NUM  0             22  TEMPORARY CHECKSUM
       ADC  P73231        23  PHYSTB THREAD
       ADC  PHSREC        24  MAX PHY RECORD SIZE      (7 TRACK)
       ADC  BF1F33        25  PACK/UNPACK BUFFER       (7 TRACK)
       EJT
*           6 1 6 - 7 2 / 9 2 / 9 5   M A G   T A P E ,   U N I T   1
*
       EQU  U73231(X73231-LOG1A)
       SPC  1
P73231 ADC  $520A         00  SCHEDULER CALL
       ADC  I17323        01  INITIATOR ADDRESS
       ADC  C17323        02  CONTINUATOR ADDRESS
       ADC  E17323        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0381         07  CONVERTER, EQUIPMENT, STATION
       ADC  $0806+T6169   08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       ADC  L17323        13  DRIVER LENGTH IF MASS MEMORY
       ADC  S17323        14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  $04C8         16  REC. OPT., UNIT, FUNCTION, DENSITY CONTROL
       ADC  U73231        17  DIAGNOSTIC LU
       ADC  0             18  ERROR CODE
       NUM  0             19  RECOVERY RETURN ADDRESS
       NUM  0             20  RECORD CHECKSUM
       NUM  $D554         21  FUNCTION DIRECTORY BITWORD
       NUM  0             22  TEMPORARY CHECKSUM
       ADC  P73232        23  PHYSTB THREAD
       ADC  PHSREC        24  MAX PHY RECORD SIZE      (7 TRACK)
       ADC  BF1F33        25  PACK/UNPACK BUFFER       (7 TRACK)
       EJT
*           6 1 6 - 7 2 / 9 2 / 9 5   M A G   T A P E ,   U N I T   2
*
       EQU  U73232(X73232-LOG1A)
       SPC  1
P73232 ADC  $520A         00  SCHEDULER CALL
       ADC  I17323        01  INITIATOR ADDRESS
       ADC  C17323        02  CONTINUATOR ADDRESS
       ADC  E17323        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0381         07  CONVERTER, EQUIPMENT, STATION
       ADC  $0806+T6169   08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       ADC  L17323        13  DRIVER LENGTH IF MASS MEMORY
       ADC  S17323        14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  $0548         16  REC. OPT., UNIT, FUNCTION, DENSITY CONTROL
       ADC  U73232        17  DIAGNOSTIC LU
       ADC  0             18  ERROR CODE
       NUM  0             19  RECOVERY RETURN ADDRESS
       NUM  0             20  RECORD CHECKSUM
       NUM  $D554         21  FUNCTION DIRECTORY BITWORD
       NUM  0             22  TEMPORARY CHECKSUM
       ADC  P73233        23  PHYSTB THREAD
       ADC  PHSREC        24  MAX PHY RECORD SIZE      (7 TRACK)
       ADC  BF1F33        25  PACK/UNPACK BUFFER       (7 TRACK)
       EJT
*           6 1 6 - 7 2 / 9 2 / 9 5   M A G   T A P E ,   U N I T   3
*
       EQU  U73233(X73233-LOG1A)
       SPC  1
P73233 ADC  $520A         00  SCHEDULER CALL
       ADC  I17323        01  INITIATOR ADDRESS
       ADC  C17323        02  CONTINUATOR ADDRESS
       ADC  E17323        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0381         07  CONVERTER, EQUIPMENT, STATION
       ADC  $0806+T6169   08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       ADC  L17323        13  DRIVER LENGTH IF MASS MEMORY
       ADC  S17323        14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  $05C8         16  REC. OPT., UNIT, FUNCTION, DENSITY CONTROL
       ADC  U73233        17  DIAGNOSTIC LU
       ADC  0             18  ERROR CODE
       NUM  0             19  RECOVERY RETURN ADDRESS
       NUM  0             20  RECORD CHECKSUM
       NUM  $D554         21  FUNCTION DIRECTORY BITWORD
       NUM  0             22  TEMPORARY CHECKSUM
       ADC  P73230        23  PHYSTB THREAD
       ADC  PHSREC        24  MAX PHY RECORD SIZE      (7 TRACK)
       ADC  BF1F33        25  PACK/UNPACK BUFFER       (7 TRACK)
       EJT
*           P S E U D O   T A P E
*
       SPC  1
       EQU  PSTPD0(1)
*
       IFA  PSTPD0,EQ,0   CORE RESIDENT DRIVER
       EXT  IPSUDO
       EQU  LPSUDO(0)
       EQU  SPSUDO($7FFF)
       EIF
*
       IFA  PSTPD0,EQ,1   MASS RESIDENT DRIVER
       EXT  MASDRV
IPSUDO JMP+ MASDRV        INITIATE DRIVER
       EXT  LPSUDO
       EXT  SPSUDO
       EIF
*
       EJT
*           P S E U D O   T A P E ,   U N I T   0
*
       SPC  1
PSUDO0 ADC  $5208         00  SCHEDULER CALL
       ADC  IPSUDO        01  INITIATOR ADDRESS
       ADC  0             02  CONTINUATOR ADDRESS   - NOT USED
       ADC  0             03  TIMEOUT ERROR ADDRESS - NOT USED
       NUM  -1            04  DIAGNOSTIC CLOCK      - NOT USED
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
PWES0  NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $0A46         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  1             12  DEVICE STATUS
       ADC  LPSUDO        13  DRIVER LENGTH IF MASS MEMORY
       ADC  SPSUDO        14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  0             16  FILE NUMBER
       NUM  0             17  TEMP FOR MOTION REQ PROCESSOR
       NUM  0             18  TEMP FOR MOTION REQ PROCESSOR
       BZS  (12)          19  REQUEST BUFFER - REQBUF
       NUM  0             31  BLOCK POINTER - BLKPTR
       BZS  (30)          32  POINTER BLOCK - PTRBLK
PSDRQ0 NUM  0             62  *
       NUM  $5400         63  *
       NUM  0             64  * AREA TO BE STUFFED WITH
       ADC  PWES0         65  * FILE MANAGER AND DISK REQUESTS
       BZS  (6)           66  *
       NUM  $1400         72  *
       RTJ* (PSDRQ0)      73  *
       ADC  0             74  INPUT BUFFER ADDRESS
       ADC  PSUDO1        75  PHYSTB THREAD
       EJT
*           P S E U D O   T A P E ,   U N I T   1
*
       SPC  1
PSUDO1 ADC  $5208         00  SCHEDULER CALL
       ADC  IPSUDO        01  INITIATOR ADDRESS
       ADC  0             02  CONTINUATOR ADDRESS   - NOT USED
       ADC  0             03  TIMEOUT ERROR ADDRESS - NOT USED
       NUM  -1            04  DIAGNOSTIC CLOCK      - NOT USED
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
PWES1  NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $0A46         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  1             12  DEVICE STATUS
       ADC  LPSUDO        13  DRIVER LENGTH IF MASS MEMORY
       ADC  SPSUDO        14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  0             16  FILE NUMBER
       NUM  0             17  TEMP FOR MOTION REQ PROCESSOR
       NUM  0             18  TEMP FOR MOTION REQ PROCESSOR
       BZS  (12)          19  REQUEST BUFFER - REQBUF
       NUM  0             31  BLOCK POINTER - BLKPTR
       BZS  (30)          32  POINTER BLOCK - PTRBLK
PSDRQ1 NUM  0             62  *
       NUM  $5400         63  *
       NUM  0             64  * AREA TO BE STUFFED WITH
       ADC  PWES1         65  * FILE MANAGER AND DISK REQUESTS
       BZS  (6)           66  *
       NUM  $1400         72  *
       RTJ* (PSDRQ1)      73  *
       ADC  0             74  INPUT BUFFER ADDRESS
       ADC  PSUDO2        75  PHYSTB THREAD
       EJT
*           P S E U D O   T A P E ,   U N I T   2
*
       SPC  1
PSUDO2 ADC  $5208         00  SCHEDULER CALL
       ADC  IPSUDO        01  INITIATOR ADDRESS
       ADC  0             02  CONTINUATOR ADDRESS   - NOT USED
       ADC  0             03  TIMEOUT ERROR ADDRESS - NOT USED
       NUM  -1            04  DIAGNOSTIC CLOCK      - NOT USED
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
PWES2  NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $0A46         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  $C401         12  DEVICE STATUS
       ADC  LPSUDO        13  DRIVER LENGTH IF MASS MEMORY
       ADC  SPSUDO        14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  $7FF7         16  FILE NUMBER
       NUM  0             17  TEMP FOR MOTION REQ PROCESSOR
       NUM  0             18  TEMP FOR MOTION REQ PROCESSOR
       BZS  (12)          19  REQUEST BUFFER - REQBUF
       NUM  0             31  BLOCK POINTER - BLKPTR
       BZS  (30)          32  POINTER BLOCK - PTRBLK
PSDRQ2 NUM  0             62  *
       NUM  $5400         63  *
       NUM  0             64  * AREA TO BE STUFFED WITH
       ADC  PWES2         65  * FILE MANAGER AND DISK REQUESTS
       BZS  (6)           66  *
       NUM  $1400         72  *
       RTJ* (PSDRQ2)      73  *
       ADC  0             74  INPUT BUFFER ADDRESS
       ADC  PSUDO3        75  PHYSTB THREAD
       EJT
*           P S E U D O   T A P E ,   U N I T   3
*
       SPC  1
PSUDO3 ADC  $5208         00  SCHEDULER CALL
       ADC  IPSUDO        01  INITIATOR ADDRESS
       ADC  0             02  CONTINUATOR ADDRESS   - NOT USED
       ADC  0             03  TIMEOUT ERROR ADDRESS - NOT USED
       NUM  -1            04  DIAGNOSTIC CLOCK      - NOT USED
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
PWES3  NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $0A46         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  $C401         12  DEVICE STATUS
       ADC  LPSUDO        13  DRIVER LENGTH IF MASS MEMORY
       ADC  SPSUDO        14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  $7FF8         16  FILE NUMBER
       NUM  0             17  TEMP FOR MOTION REQ PROCESSOR
       NUM  0             18  TEMP FOR MOTION REQ PROCESSOR
       BZS  (12)          19  REQUEST BUFFER - REQBUF
       NUM  0             31  BLOCK POINTER - BLKPTR
       BZS  (30)          32  POINTER BLOCK - PTRBLK
PSDRQ3 NUM  0             62  *
       NUM  $5400         63  *
       NUM  0             64  * AREA TO BE STUFFED WITH
       ADC  PWES3         65  * FILE MANAGER AND DISK REQUESTS
       BZS  (6)           66  *
       NUM  $1400         72  *
       RTJ* (PSDRQ3)      73  *
       ADC  0             74  INPUT BUFFER ADDRESS
       ADC  PSUDO0        75  PHYSTB THREAD
       EJT
*           P S E U D O   D I S K   U N I T   0
*
       SPC  1
       EXT  IPSDSK,CPSDSK,EPSDSK
       EQU SBIAS0(1)
       SPC  1
PSDSK0 ADC  $5209         00  SCHEDULER CALL
       ADC  IPSDSK        01  INITIATOR ADDRESS
       ADC  CPSDSK        02  CONTINUATOR ADDRESS
       ADC  EPSDSK        03 TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $1486         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  0             14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  0             16  NEW REQUEST
       ADC  CPSDSK        17  COMPLETION
       NUM  0             18  THREAD
       NUM  8             19  MASS MEMORY LOGICAL UNIT
       NUM  0             20  NUMBER OF WORDS
       NUM  0             21  BUFFER
       NUM  0             22  NEW REQUEST MSB
       NUM  0             23  NEW REQUEST LSB
       NUM  0             24  CONTROL POINT FOR TIME SHARE SYSTEM
       ADC  SBIAS0        25  SECTOR BIAS
       ADC  SBIAS0*$60    26  WORD BIAS
       NUM  9             27  COMPLETION LEVEL
       EJT
*           P S E U D O   D I S K   U N I T   1
*
       EQU SBIAS1(1)
       SPC  1
PSDSK1 ADC  $5209         00  SCHEDULER CALL
       ADC  IPSDSK        01  INITIATOR ADDRESS
       ADC  CPSDSK        02  CONTINUATOR ADDRESS
       ADC  EPSDSK        03 TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $1486         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  0             14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  0             16  NEW REQUEST
       ADC  CPSDSK        17  COMPLETION
       NUM  0             18  THREAD
       NUM  13            19  MASS MEMORY LOGICAL UNIT
       NUM  0             20  NUMBER OF WORDS
       NUM  0             21  BUFFER
       NUM  0             22  NEW REQUEST MSB
       NUM  0             23  NEW REQUEST LSB
       NUM  0             24  CONTROL POINT FOR TIME SHARE SYSTEM
       ADC  SBIAS1        25  SECTOR BIAS
       ADC  SBIAS1*$60    26  WORD BIAS
       NUM  9             27  COMPLETION LEVEL
       EJT
*           P S E U D O   D I S K   U N I T   2
*
       EQU SBIAS2(1)
       SPC  1
PSDSK2 ADC  $5209         00  SCHEDULER CALL
       ADC  IPSDSK        01  INITIATOR ADDRESS
       ADC  CPSDSK        02  CONTINUATOR ADDRESS
       ADC  EPSDSK        03 TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $1486         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  0             14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  0             16  NEW REQUEST
       ADC  CPSDSK        17  COMPLETION
       NUM  0             18  THREAD
       NUM  14            19  MASS MEMORY LOGICAL UNIT
       NUM  0             20  NUMBER OF WORDS
       NUM  0             21  BUFFER
       NUM  0             22  NEW REQUEST MSB
       NUM  0             23  NEW REQUEST LSB
       NUM  0             24  CONTROL POINT FOR TIME SHARE SYSTEM
       ADC  SBIAS2        25  SECTOR BIAS
       ADC  SBIAS2*$60    26  WORD BIAS
       NUM  9             27  COMPLETION LEVEL
       EJT
*           P S E U D O   D I S K   U N I T   3
*
       EQU SBIAS3(1)
       SPC  1
PSDSK3 ADC  $5209         00  SCHEDULER CALL
       ADC  IPSDSK        01  INITIATOR ADDRESS
       ADC  CPSDSK        02  CONTINUATOR ADDRESS
       ADC  EPSDSK        03 TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  0             07  CONVERTER, EQUIPMENT, STATION - NONE
       NUM  $1486         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  0             14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  0             16  NEW REQUEST
       ADC  CPSDSK        17  COMPLETION
       NUM  0             18  THREAD
       NUM  15            19  MASS MEMORY LOGICAL UNIT
       NUM  0             20  NUMBER OF WORDS
       NUM  0             21  BUFFER
       NUM  0             22  NEW REQUEST MSB
       NUM  0             23  NEW REQUEST LSB
       NUM  0             24  CONTROL POINT FOR TIME SHARE SYSTEM
       ADC  SBIAS3        25  SECTOR BIAS
       ADC  SBIAS3*$60    26  WORD BIAS
       NUM  9             27  COMPLETION LEVEL
       EJT
*      1 7 3 3 - 2 / 8 5 6   D I S K
*
       SPC  1
       ENT  P332D0
       EXT  I17332,C17332,E17332
       EQU  T8562(15*$10) TYPE CODE - 1733-2 856-2
       EQU  T8564(16*$10) TYPE CODE - 1733-2 856-4
       SPC  2
R17332 LDQ  =XP73320      INTERRUPT RESPONSE FOR 1733-2 DISK
       JMP* (P73320+2)
       EJT
*      1 7 3 3 - 2 / 8 5 6   D I S K - U N I T   0
*
       SPC  1
       EQU  P332D0(*)
P73320 ADC  $5209         00  SCHEDULER CALL
       ADC  I17332        01  INITIATOR ADDRESS
       ADC  C17332        02  CONTINUATOR ADDRESS
       ADC  E17332        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0181         07  CONVERTER, EQUIPMENT, STATION
       ADC  $1006+T8564   08  REQUEST STATUS
       NUM  $0200         09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  ERROR COUNTER
       NUM  0             14  DATA TRANSFER FUNCTION
       NUM  0             15  SECTOR NUMBER OR FNR RETURN
       NUM  $8100         16  NO COMPARE FLAG / DIRECTOR FUNCTION
       NUM  0             17  TEMSEC - USED BY WORD ADDRESSING
       NUM  0             18  OVERLAY AREA (SCHEDULER CALL)
       NUM  0             19  OVERLAY AREA (COMPLETION ADDRESS)
       NUM  0             20  OVERLAY AREA (THREAD)
       NUM  0             21  OVERLAY AREA (LOGICAL UNIT)
       ADC  P73321        22  PHYSTB THREAD
       NUM  0             23  RETURN ADDRESS FOR DATA TRANSFER
       NUM  $5BFB         24  FIRST SECTOR ADDRESS ON DISK 1
       NUM  0             25  LAST DATA TRANSFER FUNCTION
       NUM  0             26  BUFFER SIZE FOR SPLIT TRANSFERS
       NUM  0             27  CYLINDER ADDRESS FOR TRANSFER
       NUM  1             28  MASK FOR THIS UNITS SEEK COMPLETE BIT
       ADC  BF332A        29  ADDRESS OF 96 WORD BUFFER
       NUM  0             30  TEMPORARY FOR WORD ADDRESSING
       NUM  0             31  TEMPORARY FOR WORD ADDRESSING
       NUM  0             32  TEMPORARY FOR WORD ADDRESSING
       NUM  0             33  REQUEST CODE
       NUM  0             34  REQUEST PRIORITY
       NUM  0             35  STARTING SECTOR FOR COMPARE OR RETRY
       NUM  0             36  FWA OF TRANSFER FOR COMPARE OR RETRY
       NUM  0             37  ERROR COUNTER
       NUM  0             38  DATA TRANSFER FUNCTION CODE
       NUM  $FFFF         39  SECTOR NUMBER CURRENTLY IN BUFFER
       NUM  0             40  LAST VALUE OF CYLINDER ADDRESS STATUS
       NUM  0             41  LAST VALUE OF  C W A  STATUS
       NUM  0             42  LAST VALUE OF CHECKWORD STATUS
       NUM  0             43  LAST VALUE OF DRIVE CYLINDER STATUS
*
       BZS  BF332A(96)    44  BUFFER FOR WORD ADDRESSING
*                        139  BUFFER FOR WORD ADDRESSING
       EJT
*      1 7 3 3 - 2 / 8 5 6   D I S K - U N I T   1
*
       SPC  1
P73321 ADC  $5209         00  SCHEDULER CALL
       ADC  I17332        01  INITIATOR ADDRESS
       ADC  C17332        02  CONTINUATOR ADDRESS
       ADC  E17332        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0181         07  CONVERTER, EQUIPMENT, STATION
       ADC  $1006+T8564   08  REQUEST STATUS
       NUM  $0200         09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  ERROR COUNTER
       NUM  0             14  DATA TRANSFER FUNCTION
       NUM  0             15  SECTOR NUMBER OR FNR RETURN
       NUM  $8300         16  NO COMPARE FLAG / DIRECTOR FUNCTION
       NUM  0             17  TEMSEC - USED BY WORD ADDRESSING
       NUM  0             18  OVERLAY AREA (SCHEDULER CALL)
       NUM  0             19  OVERLAY AREA (COMPLETION ADDRESS)
       NUM  0             20  OVERLAY AREA (THREAD)
       NUM  0             21  OVERLAY AREA (LOGICAL UNIT)
       ADC  P73322        22  PHYSTB THREAD
       NUM  0             23  RETURN ADDRESS FOR DATA TRANSFER
       NUM  $5BFB         24  FIRST SECTOR ADDRESS ON DISK 1
       NUM  0             25  LAST DATA TRANSFER FUNCTION
       NUM  0             26  BUFFER SIZE FOR SPLIT TRANSFERS
       NUM  0             27  CYLINDER ADDRESS FOR TRANSFER
       NUM  2             28  MASK FOR THIS UNITS SEEK COMPLETE BIT
       ADC  BF332B        29  ADDRESS OF 96 WORD BUFFER
       NUM  0             30  TEMPORARY FOR WORD ADDRESSING
       NUM  0             31  TEMPORARY FOR WORD ADDRESSING
       NUM  0             32  TEMPORARY FOR WORD ADDRESSING
       NUM  0             33  REQUEST CODE
       NUM  0             34  REQUEST PRIORITY
       NUM  0             35  STARTING SECTOR FOR COMPARE OR RETRY
       NUM  0             36  FWA OF TRANSFER FOR COMPARE OR RETRY
       NUM  0             37  ERROR COUNTER
       NUM  0             38  DATA TRANSFER FUNCTION CODE
       NUM  $FFFF         39  SECTOR NUMBER CURRENTLY IN BUFFER
       NUM  0             40  LAST VALUE OF CYLINDER ADDRESS STATUS
       NUM  0             41  LAST VALUE OF  C W A  STATUS
       NUM  0             42  LAST VALUE OF CHECKWORD STATUS
       NUM  0             43  LAST VALUE OF DRIVE CYLINDER STATUS
*
       BZS  BF332B(96)    44  BUFFER FOR WORD ADDRESSING
*                        139  BUFFER FOR WORD ADDRESSING
       EJT
*      1 7 3 3 - 2 / 8 5 6   D I S K - U N I T   2
*
       SPC  1
P73322 ADC  $5209         00  SCHEDULER CALL
       ADC  I17332        01  INITIATOR ADDRESS
       ADC  C17332        02  CONTINUATOR ADDRESS
       ADC  E17332        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0181         07  CONVERTER, EQUIPMENT, STATION
       ADC  $1006+T8564   08  REQUEST STATUS
       NUM  $0200         09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  ERROR COUNTER
       NUM  0             14  DATA TRANSFER FUNCTION
       NUM  0             15  SECTOR NUMBER OR FNR RETURN
       NUM  $8500         16  NO COMPARE FLAG / DIRECTOR FUNCTION
       NUM  0             17  TEMSEC - USED BY WORD ADDRESSING
       NUM  0             18  OVERLAY AREA (SCHEDULER CALL)
       NUM  0             19  OVERLAY AREA (COMPLETION ADDRESS)
       NUM  0             20  OVERLAY AREA (THREAD)
       NUM  0             21  OVERLAY AREA (LOGICAL UNIT)
       ADC  P73323        22  PHYSTB THREAD
       NUM  0             23  RETURN ADDRESS FOR DATA TRANSFER
       NUM  $5BFB         24  FIRST SECTOR ADDRESS ON DISK 1
       NUM  0             25  LAST DATA TRANSFER FUNCTION
       NUM  0             26  BUFFER SIZE FOR SPLIT TRANSFERS
       NUM  0             27  CYLINDER ADDRESS FOR TRANSFER
       NUM  4             28  MASK FOR THIS UNITS SEEK COMPLETE BIT
       ADC  BF332C        29  ADDRESS OF 96 WORD BUFFER
       NUM  0             30  TEMPORARY FOR WORD ADDRESSING
       NUM  0             31  TEMPORARY FOR WORD ADDRESSING
       NUM  0             32  TEMPORARY FOR WORD ADDRESSING
       NUM  0             33  REQUEST CODE
       NUM  0             34  REQUEST PRIORITY
       NUM  0             35  STARTING SECTOR FOR COMPARE OR RETRY
       NUM  0             36  FWA OF TRANSFER FOR COMPARE OR RETRY
       NUM  0             37  ERROR COUNTER
       NUM  0             38  DATA TRANSFER FUNCTION CODE
       NUM  $FFFF         39  SECTOR NUMBER CURRENTLY IN BUFFER
       NUM  0             40  LAST VALUE OF CYLINDER ADDRESS STATUS
       NUM  0             41  LAST VALUE OF  C W A  STATUS
       NUM  0             42  LAST VALUE OF CHECKWORD STATUS
       NUM  0             43  LAST VALUE OF DRIVE CYLINDER STATUS
*
       BZS  BF332C(96)    44  BUFFER FOR WORD ADDRESSING
*                        139  BUFFER FOR WORD ADDRESSING
       EJT
*      1 7 3 3 - 2 / 8 5 6   D I S K - U N I T   3
*
       SPC  1
P73323 ADC  $5209         00  SCHEDULER CALL
       ADC  I17332        01  INITIATOR ADDRESS
       ADC  C17332        02  CONTINUATOR ADDRESS
       ADC  E17332        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0181         07  CONVERTER, EQUIPMENT, STATION
       ADC  $1006+T8564   08  REQUEST STATUS
       NUM  $0200         09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  ERROR COUNTER
       NUM  0             14  DATA TRANSFER FUNCTION
       NUM  0             15  SECTOR NUMBER OR FNR RETURN
       NUM  $8700         16  NO COMPARE FLAG / DIRECTOR FUNCTION
       NUM  0             17  TEMSEC - USED BY WORD ADDRESSING
       NUM  0             18  OVERLAY AREA (SCHEDULER CALL)
       NUM  0             19  OVERLAY AREA (COMPLETION ADDRESS)
       NUM  0             20  OVERLAY AREA (THREAD)
       NUM  0             21  OVERLAY AREA (LOGICAL UNIT)
       ADC  P73320        22  PHYSTB THREAD
       NUM  0             23  RETURN ADDRESS FOR DATA TRANSFER
       NUM  $5BFB         24  FIRST SECTOR ADDRESS ON DISK 1
       NUM  0             25  LAST DATA TRANSFER FUNCTION
       NUM  0             26  BUFFER SIZE FOR SPLIT TRANSFERS
       NUM  0             27  CYLINDER ADDRESS FOR TRANSFER
       NUM  8             28  MASK FOR THIS UNITS SEEK COMPLETE BIT
       ADC  BF332D        29  ADDRESS OF 96 WORD BUFFER
       NUM  0             30  TEMPORARY FOR WORD ADDRESSING
       NUM  0             31  TEMPORARY FOR WORD ADDRESSING
       NUM  0             32  TEMPORARY FOR WORD ADDRESSING
       NUM  0             33  REQUEST CODE
       NUM  0             34  REQUEST PRIORITY
       NUM  0             35  STARTING SECTOR FOR COMPARE OR RETRY
       NUM  0             36  FWA OF TRANSFER FOR COMPARE OR RETRY
       NUM  0             37  ERROR COUNTER
       NUM  0             38  DATA TRANSFER FUNCTION CODE
       NUM  $FFFF         39  SECTOR NUMBER CURRENTLY IN BUFFER
       NUM  0             40  LAST VALUE OF CYLINDER ADDRESS STATUS
       NUM  0             41  LAST VALUE OF  C W A  STATUS
       NUM  0             42  LAST VALUE OF CHECKWORD STATUS
       NUM  0             43  LAST VALUE OF DRIVE CYLINDER STATUS
*
       BZS  BF332D(96)    44  BUFFER FOR WORD ADDRESSING
*                        139  BUFFER FOR WORD ADDRESSING
       EJT
*           1 7 5 2   D R U M
*
       EXT  I1752,C1752,E1752
       SPC  2
R1752  LDQ  =XP1752       INTERRUPT RESPONSE FOR 1752 DRUM
       JMP* (P1752+2)
       EJT
*           1 7 5 2   D R U M
*
       SPC  1
P1752  ADC  $5209         00  SCHEDULER CALL
       ADC  I1752         01  INITIATOR ADDRESS
       ADC  C1752         02  CONTINUATOR ADDRESS
       ADC  E1752         03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0101         07  CONVERTER, EQUIPMENT, STATION
       NUM  $1036         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       NUM  0             16  SECTOR NUMBER
       NUM  0             17  DATA TRANSFER FUNCTION
       NUM  0             18  COUNTER
       NUM  0             19  FULL SECTOR COUNTER
       NUM  0             20  SAVE ECCOR
       NUM  0             21  SAVE ELSTWD
       NUM  0             22  OVERLAY AREA (SCHEDULER CALL)
       NUM  0             23  OVERLAY AREA (COMPLETION ADDRESS)
       NUM  0             24  OVERLAY AREA (THREAD)
       NUM  0             25  OVERLAY AREA (LOGICAL UNIT)
       NUM  0             26  UNSUCCESSFUL I/O ATTEMPT COUNTER
       NUM  0             27  EQUIPMENT STATUS (ON LAST ERROR)
       NUM  0             28  CORE STATUS      (ON LAST ERROR)
       NUM  0             29  SECTOR STATUS    (ON LAST ERROR)
       NUM  0             30  DATA STATUS      (ON LAST ERROR)
       EJT
*           1 7 4 2 - 3 0 / 1 2 0   L I N E   P R I N T E R
*
       SPC  1
       EQU  T4230(17*$10) TYPE CODE - 1742-30
       EQU  T4212(18*$10) TYPE CODE - 1742-120
       EQU  U42312(X42312-LOG1A)
       EQU  F42312(FTN742-LOG1A)
       SPC  1
       EQU  M42312(1)
*
       IFA  M42312,EQ,0   CORE RESIDENT DRIVER
       EXT  I42312
       EXT  C42312
       EXT  E42312
       EQU  L42312(0)
       EQU  S42312($7FFF)
       EIF
*
       IFA  M42312,EQ,1   MASS RESIDENT DRIVER
       EXT  MASDRV
       EXT  MASCON
       EXT  MASERR
I42312 JMP+ MASDRV        INITIATE DRIVER
C42312 JMP+ MASCON        INTERRUPT RESPONSE
E42312 JMP+ MASERR        TIMEOUT ERROR
       EXT  L42312
       EXT  S42312
       EIF
*
       SPC  2
R42312 LDQ  =XP42312      INTERRUPT RESPONSE FOR 1742-30/120 PRINTER
       JMP* (P42312+2)
       EJT
*           1 7 4 2 - 3 0 / 1 2 0   L I N E   P R I N T E R
*
       SPC  1
P42312 ADC  $520A         00  SCHEDULER CALL
       ADC  I42312        01  INITIATOR ADDRESS
       ADC  C42312        02  CONTINUATOR ADDRESS
       ADC  E42312        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0201         07  CONVERTER, EQUIPMENT, STATION
       ADC  $2804+T4212   08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       ADC  L42312        13  DRIVER LENGTH IF MASS MEMORY
       ADC  S42312        14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  BLANK DETECTION INDICATOR
       NUM  0             16  LINE  COUNT
       NUM  0             17  TEMPORARY STORAGE FOR CONTROL FUNC.
       NUM  0             18  CHARACTER COUNT
       ADC  F42312        19  FORTRAN LOGICAL UNIT
       NUM  0             20  NUMBER OF BLANKS TO BE SENT
       NUM  60            21  MAXIMUM NUMBER OF LINES PER PAGE
       NUM  136           22  NUMBER OF CHARACTERS PER LINE
       ADC  U42312        23  DIAGNOSTIC LU
       EJT
*
*           F O R T R A N   L I N E   P R I N T E R
       SPC  2
*           PHYSTB SHARED WITH 1742-30/120 LINE PRINTER
       EJT
*           1 7 2 8 / 4 3 0   C A R D   R E A D E R / P U N C H
*
       SPC  1
       EQU  U1728(X1728-LOG1A)
       SPC  1
       EQU  M1728(1)
*
       IFA  M1728,EQ,0    CORE RESIDENT DRIVER
       EXT  I1728
       EXT  C1728
       EXT  E1728
       EQU  L1728(0)
       EQU  S1728($7FFF)
       EIF
*
       IFA  M1728,EQ,1    MASS RESIDENT DRIVER
       EXT  MASDRV
       EXT  MASCON
       EXT  MASERR
I1728  JMP+ MASDRV        INITIATE DRIVER
C1728  JMP+ MASCON        INTERRUPT RESPONSE
E1728  JMP+ MASERR        TIMEOUT ERROR
       EXT  L1728
       EXT  S1728
       EIF
*
       SPC  2
R1728  LDQ  =XP1728       INTERRUPT RESPONSE FOR 1728-430 READ/PUNCH
       JMP* (P1728+2)
       EJT
*           1 7 2 8 / 4 3 0   C A R D   R E A D E R / P U N C H
*
       SPC  1
P1728  ADC  $520E         00  SCHEDULER CALL
       ADC  I1728         01  INITIATOR ADDRESS
       ADC  C1728         02  CONTINUATOR ADDRESS
       ADC  E1728         03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0521         07  CONVERTER, EQUIPMENT, STATION
       NUM  $18C6         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       NUM  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       ADC  L1728         13  DRIVER LENGTH IF MASS MEMORY
       ADC  S1728         14  NAME ASSOCIATED WITH SECTOR NUMBER
       NUM  0             15  PACKING CYCLE ADDRESS STORAGE
       NUM  $800F         16  READ/PUNCH SWITCH, EOF FORMAT (6789)
       ADC  BUF28         17  FIRST LOCATION OF 80 WORD I / O BUFFER
       NUM  0             18  CURRENT CARD BUFFER LOCATION
       NUM  0             19  SUBROUTINE RETURN ADDRESS
       NUM  0             20  CARD SEQUENCE NUMBER
       NUM  0             21  RECORD LENGTH
       NUM  0             22  CHECKSUM ACCUMULATOR
       NUM  0             23  TEMPORARY STORAGE
       NUM  0             24  OUTPUT OFFSET SWITCH
       NUM  0             25  ERROR RETURN
       NUM  0             26  HOLLERITH ERROR FLAG
       ADC  U1728         27  DIAGNOSTIC LU
*
       BZS  BUF28(80)     28  INPUT / OUTPUT BUFFER
*                        107  INPUT / OUTPUT BUFFER
       EJT
*           1 7 4 3 - 2   C O M M U N I C A T I O N S
*
*           C O N T R O L L E R ,   U N I T   0
*
       EXT  I17432,C17432,E17432
       ENT  CABF00
       ENT  P74300
       EQU  U74300(X74300-LOG1A)
       SPC  1
R17432 LDQ  =XP74300      INTERRUPT RESPONSE FOR 1743-2
       JMP* (P74300+2)
       SPC  2
P74300 ADC  $520A         00  SCHEDULER CALL
       ADC  I17432        01  INITIATOR ADDRESS
       ADC  C17432        02  CONTINUATOR ADDRESS
       ADC  E17432        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0282         07  CONVERTER, EQUIPMENT, STATION
       NUM  $3376         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       ADC  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       ADC  CABF00        16  START OF BUFFER ADDRESS
       ADC  U74300        17  DIAGNOSTIC LOGICAL UNIT
       NUM  $0000         18  TERMINATION CHARACTERS
       NUM  0             19  ERROR CODE STORAGE
       NUM  $8000         20  TYPE OF I/O  - = FULL DUPLEX
       NUM  $023C         21  OUTPUT/INPUT DIAG CLOCK TIME
       NUM  $0800         22  MOTION WORD 1 - BACKSPACE RECORD
       NUM  $1900         23  MOTION WORD 2 - WRITE EOF
       NUM  0             24  MOTION WORD 3 - REWIND
       NUM  $0D00         25  MOTION WORD 4 - REWIND/UNLOAD
       NUM  $0A00         26  MOTION WORD 5 - ADVANCE FILE
       NUM  $1A00         27  MOTION WORD 6 - BACKSPACE FILE
       NUM  $1500         28  MOTION WORD 7 - ADVANCE RECORD
       NUM  $0000         29  TIMESHARE FLAG AND PRIORITY LEVEL
       NUM  40            30  LENGTH OF INPUT BUFFER
       ADC  0             31  ENTRY POINT OF INPUT HANDLER
       NUM  0             32  CURRENT MOTION WORD
       ADC  L74300-LOG1A  33  ACTUAL LOGICAL UNIT
       BZS  CABF00(40)    34  INPUT BUFFER
*                         73  INPUT BUFFER
       NUM  0             74  INPUT LENGTH
       ADC  P74301        75  PHYSTB THREAD
       EJT
*           1 7 4 3 - 2   C O M M U N I C A T I O N S
*
*           C O N T R O L L E R ,   U N I T   1
*
       ENT  CABF01
       EQU  U74301(X74301-LOG1A)
       SPC  1
P74301 ADC  $520A         00  SCHEDULER CALL
       ADC  I17432        01  INITIATOR ADDRESS
       ADC  C17432        02  CONTINUATOR ADDRESS
       ADC  E17432        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $0292         07  CONVERTER, EQUIPMENT, STATION
       NUM  $3376         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       ADC  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       ADC  CABF01        16  START OF BUFFER ADDRESS
       ADC  U74301        17  DIAGNOSTIC LOGICAL UNIT
       NUM  $0000         18  TERMINATION CHARACTERS
       NUM  0             19  ERROR CODE STORAGE
       NUM  $8000         20  TYPE OF I/O  - = FULL DUPLEX
       NUM  $023C         21  OUTPUT/INPUT DIAG CLOCK TIME
       NUM  $0800         22  MOTION WORD 1 - BACKSPACE RECORD
       NUM  $1900         23  MOTION WORD 2 - WRITE EOF
       NUM  0             24  MOTION WORD 3 - REWIND
       NUM  $0D00         25  MOTION WORD 4 - REWIND/UNLOAD
       NUM  $0A00         26  MOTION WORD 5 - ADVANCE FILE
       NUM  $1A00         27  MOTION WORD 6 - BACKSPACE FILE
       NUM  $1500         28  MOTION WORD 7 - ADVANCE RECORD
       NUM  $0000         29  TIMESHARE FLAG AND PRIORITY LEVEL
       NUM  40            30  LENGTH OF INPUT BUFFER
       ADC  0             31  ENTRY POINT OF INPUT HANDLER
       NUM  0             32  CURRENT MOTION WORD
       ADC  L74301-LOG1A  33  ACTUAL LOGICAL UNIT
       BZS  CABF01(40)    34  INPUT BUFFER
*                         73  INPUT BUFFER
       NUM  0             74  INPUT LENGTH
       ADC  P74302        75  PHYSTB THREAD
       EJT
*           1 7 4 3 - 2   C O M M U N I C A T I O N S
*
*           C O N T R O L L E R ,   U N I T   2
*
       ENT  CABF02
       EQU  U74302(X74302-LOG1A)
       SPC  1
P74302 ADC  $520A         00  SCHEDULER CALL
       ADC  I17432        01  INITIATOR ADDRESS
       ADC  C17432        02  CONTINUATOR ADDRESS
       ADC  E17432        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $02A2         07  CONVERTER, EQUIPMENT, STATION
       NUM  $3376         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       ADC  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       ADC  CABF02        16  START OF BUFFER ADDRESS
       ADC  U74302        17  DIAGNOSTIC LOGICAL UNIT
       NUM  $0000         18  TERMINATION CHARACTERS
       NUM  0             19  ERROR CODE STORAGE
       NUM  $8000         20  TYPE OF I/O  - = FULL DUPLEX
       NUM  $023C         21  OUTPUT/INPUT DIAG CLOCK TIME
       NUM  $0800         22  MOTION WORD 1 - BACKSPACE RECORD
       NUM  $1900         23  MOTION WORD 2 - WRITE EOF
       NUM  0             24  MOTION WORD 3 - REWIND
       NUM  $0D00         25  MOTION WORD 4 - REWIND/UNLOAD
       NUM  $0A00         26  MOTION WORD 5 - ADVANCE FILE
       NUM  $1A00         27  MOTION WORD 6 - BACKSPACE FILE
       NUM  $1500         28  MOTION WORD 7 - ADVANCE RECORD
       NUM  $0000         29  TIMESHARE FLAG AND PRIORITY LEVEL
       NUM  40            30  LENGTH OF INPUT BUFFER
       ADC  0             31  ENTRY POINT OF INPUT HANDLER
       NUM  0             32  CURRENT MOTION WORD
       ADC  L74302-LOG1A  33  ACTUAL LOGICAL UNIT
       BZS  CABF02(40)    34  INPUT BUFFER
*                         73  INPUT BUFFER
       NUM  0             74  INPUT LENGTH
       ADC  P74303        75  PHYSTB THREAD
       EJT
*           1 7 4 3 - 2   C O M M U N I C A T I O N S
*
*           C O N T R O L L E R ,   U N I T   3
*
       ENT  CABF03
       EQU  U74303(X74303-LOG1A)
       SPC  1
P74303 ADC  $520A         00  SCHEDULER CALL
       ADC  I17432        01  INITIATOR ADDRESS
       ADC  C17432        02  CONTINUATOR ADDRESS
       ADC  E17432        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $02B2         07  CONVERTER, EQUIPMENT, STATION
       NUM  $3376         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       ADC  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       ADC  CABF03        16  START OF BUFFER ADDRESS
       ADC  U74303        17  DIAGNOSTIC LOGICAL UNIT
       NUM  $0000         18  TERMINATION CHARACTERS
       NUM  0             19  ERROR CODE STORAGE
       NUM  $8000         20  TYPE OF I/O  - = FULL DUPLEX
       NUM  $023C         21  OUTPUT/INPUT DIAG CLOCK TIME
       NUM  $0800         22  MOTION WORD 1 - BACKSPACE RECORD
       NUM  $1900         23  MOTION WORD 2 - WRITE EOF
       NUM  0             24  MOTION WORD 3 - REWIND
       NUM  $0D00         25  MOTION WORD 4 - REWIND/UNLOAD
       NUM  $0A00         26  MOTION WORD 5 - ADVANCE FILE
       NUM  $1A00         27  MOTION WORD 6 - BACKSPACE FILE
       NUM  $1500         28  MOTION WORD 7 - ADVANCE RECORD
       NUM  $0000         29  TIMESHARE FLAG AND PRIORITY LEVEL
       NUM  40            30  LENGTH OF INPUT BUFFER
       ADC  0             31  ENTRY POINT OF INPUT HANDLER
       NUM  0             32  CURRENT MOTION WORD
       ADC  L74303-LOG1A  33  ACTUAL LOGICAL UNIT
       BZS  CABF03(40)    34  INPUT BUFFER
*                         73  INPUT BUFFER
       NUM  0             74  INPUT LENGTH
       ADC  P74304        75  PHYSTB THREAD
       EJT
*           1 7 4 3 - 2   C O M M U N I C A T I O N S
*
*           C O N T R O L L E R ,   U N I T   4
*
       ENT  CABF04
       EQU  U74304(X74304-LOG1A)
       SPC  1
P74304 ADC  $520A         00  SCHEDULER CALL
       ADC  I17432        01  INITIATOR ADDRESS
       ADC  C17432        02  CONTINUATOR ADDRESS
       ADC  E17432        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $02C2         07  CONVERTER, EQUIPMENT, STATION
       NUM  $3376         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       ADC  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       ADC  CABF04        16  START OF BUFFER ADDRESS
       ADC  U74304        17  DIAGNOSTIC LOGICAL UNIT
       NUM  $0000         18  TERMINATION CHARACTERS
       NUM  0             19  ERROR CODE STORAGE
       NUM  $8000         20  TYPE OF I/O  - = FULL DUPLEX
       NUM  $023C         21  OUTPUT/INPUT DIAG CLOCK TIME
       NUM  $0800         22  MOTION WORD 1 - BACKSPACE RECORD
       NUM  $1900         23  MOTION WORD 2 - WRITE EOF
       NUM  0             24  MOTION WORD 3 - REWIND
       NUM  $0D00         25  MOTION WORD 4 - REWIND/UNLOAD
       NUM  $0A00         26  MOTION WORD 5 - ADVANCE FILE
       NUM  $1A00         27  MOTION WORD 6 - BACKSPACE FILE
       NUM  $1500         28  MOTION WORD 7 - ADVANCE RECORD
       NUM  $0000         29  TIMESHARE FLAG AND PRIORITY LEVEL
       NUM  40            30  LENGTH OF INPUT BUFFER
       ADC  0             31  ENTRY POINT OF INPUT HANDLER
       NUM  0             32  CURRENT MOTION WORD
       ADC  L74304-LOG1A  33  ACTUAL LOGICAL UNIT
       BZS  CABF04(40)    34  INPUT BUFFER
*                         73  INPUT BUFFER
       NUM  0             74  INPUT LENGTH
       ADC  P74305        75  PHYSTB THREAD
       EJT
*           1 7 4 3 - 2   C O M M U N I C A T I O N S
*
*           C O N T R O L L E R ,   U N I T   5
*
       ENT  CABF05
       EQU  U74305(X74305-LOG1A)
       SPC  1
P74305 ADC  $520A         00  SCHEDULER CALL
       ADC  I17432        01  INITIATOR ADDRESS
       ADC  C17432        02  CONTINUATOR ADDRESS
       ADC  E17432        03  TIMEOUT ERROR ADDRESS
       NUM  -1            04  DIAGNOSTIC CLOCK
       NUM  0             05  LOGICAL UNIT
       NUM  0             06  PARAMETER LOCATION
       NUM  $02D2         07  CONVERTER, EQUIPMENT, STATION
       NUM  $3376         08  REQUEST STATUS
       NUM  0             09  DRIVER STATUS
       NUM  0             10  CURRENT LOCATION
       ADC  0             11  LAST LOCATION PLUS ONE
       NUM  0             12  DEVICE STATUS
       NUM  0             13  RESERVED
       NUM  $7FFF         14  RESERVED
       NUM  0             15  RESERVED FOR FNR AND CMR
       ADC  CABF05        16  START OF BUFFER ADDRESS
       ADC  U74305        17  DIAGNOSTIC LOGICAL UNIT
       NUM  $0000         18  TERMINATION CHARACTERS
       NUM  0             19  ERROR CODE STORAGE
       NUM  $8000         20  TYPE OF I/O  - = FULL DUPLEX
       NUM  $023C         21  OUTPUT/INPUT DIAG CLOCK TIME
       NUM  $0800         22  MOTION WORD 1 - BACKSPACE RECORD
       NUM  $1900         23  MOTION WORD 2 - WRITE EOF
       NUM  0             24  MOTION WORD 3 - REWIND
       NUM  $0D00         25  MOTION WORD 4 - REWIND/UNLOAD
       NUM  $0A00         26  MOTION WORD 5 - ADVANCE FILE
       NUM  $1A00         27  MOTION WORD 6 - BACKSPACE FILE
       NUM  $1500         28  MOTION WORD 7 - ADVANCE RECORD
       NUM  $0000         29  TIMESHARE FLAG AND PRIORITY LEVEL
       NUM  40            30  LENGTH OF INPUT BUFFER
       ADC  0             31  ENTRY POINT OF INPUT HANDLER
       NUM  0             32  CURRENT MOTION WORD
       ADC  L74305-LOG1A  33  ACTUAL LOGICAL UNIT
       BZS  CABF05(40)    34  INPUT BUFFER
*                         73  INPUT BUFFER
       NUM  0             74  INPUT LENGTH
       ADC  P74300        75  PHYSTB THREAD
       EJT
*           R E S I D E N T   C O R E   D A T A
*
       ENT  LSTLOC
       EXT  BGNMON
       SPC  1
LSTLOC ADC  BGNMON        BEGINNING LOCATION OF CORE RESIDENT SYSTEM
       SPC  2
*           C O R E   A L L O C A T I O N   D A T A
*
       ENT  CALTHD        CORE ALLOCATOR THREAD
       ENT  LVLSTR        LEVEL START ALLOCATION TABLE
       EXT  AREAC         START OF ALLOCATABLE AREA
       EXT  LEND          END OF ALLOCATABLE AREA
*
CALTHD ADC  0             TOTAL AVAILABLE ALLOCATABLE CORE
       ADC  AREAC         START OF ALLOCATABLE AREA
*
LVLSTR ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  0
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  1
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  2
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  3
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  4
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  5
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  6
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  7
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  8
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL  9
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL 10
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL 11
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL 12
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL 13
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL 14
       ADC  AREAC         START OF ALLOCATABLE CORE FOR LEVEL 15
       ADC  LEND          END OF ALLOCATABLE CORE
*
       ENT  N5,N6,N7,N8,N9,N10,N11,N12,N13,N14,N15
       SPC  1
*      NOTE -  THE SIZE OF AREAS 1, 2, 3, AND 4 ARE SPECIFIED
*              DURING SYSTEM INITIALIZATION
*
N5     EQU     N5()       NUMBER OF CORE LOCATIONS FOR AREA 5
N6     EQU     N6()       NUMBER OF CORE LOCATIONS FOR AREA 6
N7     EQU     N7()       NUMBER OF CORE LOCATIONS FOR AREA 7
N8     EQU     N8()       NUMBER OF CORE LOCATIONS FOR AREA 8
N9     EQU     N9()       NUMBER OF CORE LOCATIONS FOR AREA 9
N10    EQU    N10()       NUMBER OF CORE LOCATIONS FOR AREA 10
N11    EQU    N11()       NUMBER OF CORE LOCATIONS FOR AREA 11
N12    EQU    N12()       NUMBER OF CORE LOCATIONS FOR AREA 12
N13    EQU    N13()       NUMBER OF CORE LOCATIONS FOR AREA 13
N14    EQU    N14()       NUMBER OF CORE LOCATIONS FOR AREA 14
N15    EQU    N15()       NUMBER OF CORE LOCATIONS FOR AREA 15
       EJT
*           P A R T I T I O N   C O R E   D A T A
*
       ENT PARTBL,BUSY,DIP,LSTPRT,THDS,USE
       SPC  1
       EQU LSTPRT(2)      LAST PARTITION IN SYSTEM
*
PARTBL NUM  $BF20         STARTING ADDRESS OF PARTITION 0
       NUM  $C310         STARTING ADDRESS OF PARTITION 1
       NUM  $CAE0         STARTING ADDRESS OF PARTITION 2
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 3
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 4
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 5
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 6
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 7
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 8
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 9
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 10
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 11
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 12
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 13
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 14
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 15
*
       NUM  $FFFF         STARTING ADDRESS OF PARTITION 16 - SWAP AREA
       SPC  2
THDS   NUM  $FFFF         TOP OF REQUEST THREAD FOR PARTITION 0
       NUM  $FFFF         TOP OF REQUEST THREAD FOR PARTITION 1
       NUM  $FFFF         TOP OF REQUEST THREAD FOR PARTITION 2
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 3
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 4
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 5
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 6
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 7
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 8
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 9
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 10
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 11
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 12
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 13
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 14
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 15
*
       NUM  0             TOP OF REQUEST THREAD FOR PARTITION 16 - SWAP
       SPC  2
USE    BZS  USE(16)       PARTITION-IN-USE INDICATORS
       SPC  2
BUSY   NUM  $FFF8         BUSY INDICATOR  -  BIT 0 = PARTITION 0
DIP    NUM  -0            PARTITION CORE DRIVER ACTIVE INDICATOR
       EJT
*           S Y S T E M   C O M M O N   D E C L A R A T I O N
*
*      THIS ENTRY SPECIFIES THE AMOUNT OF SYSTEM (BLANK) COMMON
       SPC  4
       EQU   NCOM($03E8)
       SPC  1
COMMON COM  COMMON(NCOM)
       EJT
*           M I S C E L L A N E O U S   P R O G R A M S
*
*           S Y S T E M   I D L E   L O O P
       SPC  2
       ENT  IDLE          BASIC SYSTEM IDLE LOOP
       ENT  IDLER         SYSTEM IDLE SUBROUTINE
       ENT  INSTLU        SYSTEM INSTALLATION L. U.
       SPC  2
IDLE   LDA* STRTUP        IS THIS THE INITIAL IDLE ENTRY
       SAN  IDLE1         NO
       RTJ* STRTUP        YES, PERFORM STARTUP FUNCTIONS
IDLE1  RTJ* IDLER
       JMP* IDLE
       SPC  2
       EXT  LIBEDT        LIBRARY EDIT
       EXT  RELFLE        SYSTEM CORE SWAP ROUTINE
       EXT  INPTV4        JOB PROCESSOR STANDARD INPUT DEVICE
       EQU INSTLU(6)
       SPC  1
STRTUP NUM  0
       LDQ- $EB
       ADQ  =XLIBEDT      OBTAIN THE DIRECTORY ADDRESS OF LIBEDT
       LDA- (ZERO),Q      HAVE THE REQUEST PRIORITIES BEEN SET UP
       SAN  STRTP1        YES
       LDA  =XINSTLU
       STA+ INPTV4        SET THE STD. INPUT TO THE INSTALLATION L.U.
       JMP* (STRTUP)         AND EXIT
       SPC  1
STRTP1 EQU  STRTP1(*)
*
*      NOTE - ANY ADDITIONAL SYSTEM STARTUP FUNCTIONS
*             MAY BE ADDED HERE.
*
       RTJ- (AMONI)       SCHEDULE RELFLE TO FORCE A SWAP
       ADC  $5203
       ADC  RELFLE
       JMP* (STRTUP)
       EJT
*           M I S C E L L A N E O U S   P R O G R A M S
*
*           I N T E R R U P T   R E S P O N S E   F O R   T I M E R
*
       ENT  TMRTYP,TMCODE TYPE OF SYSTEM TIME BASE
       EXT  TIMEUP        TMINT INTERRUPT ENTRY
       EQU  X($7FFF)      VALUE FOR UNSELECTED ENTRY POINTS
       ENT  E15761
       EQU  E15761(X)     LINK UNSELECTED ENTRY POINT
       SPC  1
       EQU  TMCODE(0)     SOFTWARE PSEUDO TIMER
TMRTYP ADC  6             TIME BASE CODE
       SPC  2
CTR    NUM  30967
BASCTR NUM  30967         APPROXIMATION TO 60 CPS
       SPC  1
IDLER  NUM  0
       IIN  0             USED AT LEVEL -1 OR LEVEL 2
       SOV  0
       RAO* CTR           PSEUDO SYSTEM TIMER
       SOV  COUNT         SKIP IF 1800 LOOPS EXECUTED
       EIN  0
       JMP* (IDLER)       BACK TO BASIC IDLE LOOP
COUNT  LDA* BASCTR        INITIALIZE LOOP COUNTER
       STA* CTR
       LDQ* IDLER         SAVE RETURN FOR RE-ENTRANCY IN VOLATILE
       RTJ- (AVOLA)       GET 3 WORDS VOLATILE STORAGE
       NUM  3
       RTJ- (AMONI)
       ADC  $520D         SCHEDULE TMINT AT LEVEL 13
       ADC  TIMEUP
       IIN  0
       RTJ- (AVOLR)       RETURN VOLATILE
       JMP- (ZERO),Q      RETURN TO IDLE LOOP
       SPC  4
*
*           LINK ALL UNSELECTED ENTRY POINTS
*
       ENT  E1572F,E1572,O1572,E1573,H15721,E15721,D15721,O15721,EQ3644
       ENT  F10336,O10336,E10336
       ENT  DMICOD,EMPSRT,TBLADR
       EQU  E1572F(X),E1572(X),O1572(X),E1573(X),H15721(X)
       EQU  E15721(X),D15721(X),O15721(X),EQ3644(X)
       EQU  F10336(X),O10336(X),E10336(X)
       EQU  DMICOD(X),EMPSRT(X),TBLADR(X)
       EJT
*           M I S C E L L A N E O U S   P R O G R A M S
*
*           A / Q   C H A N N E L   A L L O C A T I O N
*
       SPC  2
       ENT  RQAQ          REQUEST ENTRY FOR A/Q
       ENT  RLAQ          RELEASE ENTRY FOR A/Q
       SPC  1
RQAQ   NUM  0             ENTRY USED IF NO A/Q ALLOCATION
       IIN  0
       LDQ- I             TRANSFER PDT ADDRESS TO Q
       EIN  0
       JMP* (RQAQ)        RETURN
       SPC  1
       EQU  RLAQ(RQAQ)    EQUATE BOTH ENTRIES
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           M A S S   R E S I D E N T   D R I V E R S   B U F F E R
*
*           THIS BUFFER WILL CONTAIN THE MASS RESIDENT DRIVER(S)
*           WHEN THEY ARE IN CORE.   THE SMALLEST ALLOWABLE SIZE IS
*           EQUAL TO THE LARGEST MASS RESIDENT DRIVER IN THE SYSTEM.
*           OPTIMUM THROUGHPUT REQUIRES SIZING EQUAL TO THE TWO
*           LARGEST MASS RESIDENT DRIVERS IN THE SYSTEM.
       SPC  2
       ENT  BUFF,BUFFE
       EQU  MBFSZ($A00)
       SPC  2
BUFF   BZS  BUFF(MBFSZ)
       EQU  BUFFE(*)
       SPC  2
*           C O M M O N   G H O S T   I N T E R R U P T   R O U T I N E
       SPC  1
       ENT  CGHOST
       SPC  1
CGHOST NOP  0
       JMP- (ADISP)
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           F O R T R A N   R E E N T R A N T   I N F O R M A T I O N
*
       ENT  FMASK,FLIST
       EXT  E4SAVE
       EXT  ARGU0
       SPC  1
FMASK  NUM  $0070         FORTRAN REENTRANT LEVELS (BIT 0 = LEVEL 0)
       SPC  1
*      TABLE OF FORTRAN ENTRY POINTS SAVED TO MAINTAIN REENTRANCY
       SPC  1
*      ENTRY POINT        PROGRAM       DESCRIPTION
*      -----------        -------       ----------
       SPC  1
FLIST  ADC  FEND
       ADC  E4SAVE        Q8EXPR   LOCATION $E4   STORAGE
       ADC  ARGU0         Q8QIO    TEMPORARY STORAGE
FEND   EQU  FEND(*-FLIST-1)
       EJT
*           F O R T R A N   R E E N T R A N T   I N F O R M A T I O N
*
       SPC  4
*      THIS ENTRY IS PROVIDED TO ALLOW COMPATIBILITY BETWEEN THE
*      NON-REENTRANT (BACKGROUND) FORTRAN AND REENTRANT FORTRAN
       SPC  1
       ENT  Q8STP
       SPC  1
Q8STP  NOP  0             FORTRAN    STOP
       JMP- (ADISP)
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           1 7 8 1 - 1   H A R D W A R E   F L O A T I N G
*
*           P O I N T   I N F O R M A T I O N
*
       SPC  4
*           THESE ENTRIES ALLOW PROPER SYSTEM LINKAGE IF THE 1781-1
*           IS NOT SELECTED.
       SPC  2
       ENT  E17811,F17811
E17811 NUM  $7FFF
       EQU  F17811($7FFF)
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           T I M E / D A T E   P A R A M E T E R   S T O R A G E
*
       SPC  3
       ENT  AYERTO        CURRENT YEAR   (ASCII)
       ENT  AMONTO        CURRENT MONTH  (ASCII)
       ENT  ADAYTO        CURRENT DAY    (ASCII)
       ENT  YERTO         CURRENT YEAR   (INTEGER)
       ENT  MONTO         CURRENT MONTH  (INTEGER)
       ENT  DAYTO         CURRENT DAY    (INTEGER)
       ENT  HORTO         CURRENT HOUR   (INTEGER)
       ENT  MINTO         CURRENT MINUTE (INTEGER)
       ENT  SECON         CURRENT SECOND (INTEGER)
       ENT  CONTA         CURRENT COUNT  (INTEGER)
       ENT  HORMIN        CURRENT 24-HOUR TIME
       ENT  TOTMIN        CURRENT DAY ELAPSED MINUTES
       SPC  3
AYERTO NUM  0             00  CURRENT YEAR   (ASCII)
AMONTO NUM  0             01  CURRENT MONTH  (ASCII)
ADAYTO NUM  0             02  CURRENT DAY    (ASCII)
YERTO  NUM  0             03  CURRENT YEAR   (INTEGER)
MONTO  NUM  0             04  CURRENT MONTH  (INTEGER)
DAYTO  NUM  0             05  CURRENT DAY    (INTEGER)
HORTO  NUM  0             06  CURRENT HOUR   (INTEGER)
MINTO  NUM  0             07  CURRENT MINUTE (INTEGER)
SECON  NUM  0             08  CURRENT SECOND (INTEGER)
CONTA  NUM  0             09  CURRENT COUNT  (INTEGER)
HORMIN NUM  0             10  CURRENT 24-HOUR TIME
TOTMIN NUM  0             11  CURRENT DAY ELAPSED MINUTES
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           S Y S T E M   T I M E R   P A R A M E T E R S
       SPC  4
       ENT  TIMCPS        BASIC SYSTEM CLOCK FREQUENCY
       SPC  1
TIMCPS EQU  TIMCPS(60)    TIMER CYCLES PER SECOND
       SPC  4
       ENT  TIMEC         TIMER CYCLES PER 1/10 SECOND MINUS 1
       SPC  1
TIMEC  EQU  TIMEC(TIMCPS/10-1)
       SPC  4
       ENT  TODLVL        TIME OF DAY(TOD) PROGRAM REQ. CODE + PRIORITY
       SPC  1
TODLVL EQU  TODLVL($5006) D-BIT = 1, REQUEST CODE 8, PRIORITY 6
       SPC  4
       ENT  NSCHED        MAX. NO. OF COMPLETIONS PER TIMER INTERVAL
       SPC  1
NSCHED NUM  5             MAXIMUM 5 COMPLETIONS PER INTERVAL
       SPC  4
       ENT  TMRLVL        DIAGNOSTIC TIMER PRIORITY LEVEL
       SPC  1
TMRLVL EQU  TMRLVL(13)    LEVEL 13
       EJT
*      M I S C E L L A N E O U S   I N F O R M A T I O N
*
*      S Y S T E M   P R O G R A M   O V E R L A Y   S I Z E S
       SPC  2
       ENT  LSIZV4        INITIAL OVERLAY SIZE OF LIBEDT
       ENT  PSIZV4        SIZE OF AREA 3
       ENT  ODBSIZ        INITIAL OVERLAY SIZE OF ODEBUG
       SPC  4
       EQU  LSIZV4($4B8)  INITIAL OVERLAY SIZE OF LIBEDT
       EQU  PSIZV4($4B8)  SIZE OF AREA 3
       EQU  ODBSIZ($369)  INITIAL OVERLAY SIZE OF ODEBUG
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           S  C  M  M    I N C O R E   F L A G
*
       ENT  SCMMLC
       SPC  2
SCMMLC NUM  0             NON-ZERO IF  S C M M  RUNNING
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           O N   L I N E   D E B U G   I N C O R E   F L A G
*
       ENT  CHRSFG
       SPC  2
CHRSFG NUM  0             NON-ZERO IF ODEBUG RUNNING
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           S Y S T E M   C H E C K O U T   P A R A M E T E R S
       SPC  4
*      THE STARTING SECTOR OF THE FAILED CORE IMAGE IS SPECIFIED BY
*      THE NAME COBOPS.  THIS AREA MUST BE SIZED TO ACCOMODATE A
*      FAILED IMAGE OF THE SIZE SPECIFIED BY NAME MSIZV4.  THE FAILED
*      IMAGE MUST RESIDE ON THE LIBRARY MASS MEMORY UNIT.  IF THE
*      MASS MEMORY LIBRARY UNIT IS A CARTRIDGE DISK, THE IMAGE AREA
*      CANNOT OVERLAP FROM ONE PLATTER TO THE OTHER.
       SPC  2
       ENT  COBOPS
COBOPS EQU COBOPS($5A00)  START SECTOR OF FAILED IMAGE
       SPC  4
*      THIS ENTRY IS PROVIDED TO LINK THE NO-FORTRAN DISPATCHER
*      ENTRY POINT
       SPC  1
       ENT  NDISP
       SPC  1
       EQU  NDISP($7FFF)
       SPC  4
*      THIS ENTRY IS PROVIDED TO LINK THE TIMESHARE PROTECT INTERRUPT
*      PROCESSOR ENTRY POINT
       SPC  1
       ENT  TSIPRC
       SPC  1
       EQU  TSIPRC($7FFF)
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           F I L E   M A N A G E R   D A T A
*
       SPC  2
       ENT  FISLU         LOGICAL UNIT OF FIS DIRECTORY AND BLOCKS
       ENT  MAXMMA        MAXIMUM NO. OF MASS MEMORY ATTEMPTS ON ERROR
       ENT  RPTPER        REQUEST PROCESSOR TIMEOUT PERIOD
       ENT  FDTPER        FILE/DIRECTORY TIMEOUT PERIOD
       ENT  FIDSEC        FIS DIRECTORY, S SECTOR ADDRESS
       ENT  FIBLSA        SECTOR ADDRESS OF LAST FIS BLOCK
       ENT  FIBNIX        INDEX TO THE NEXT AVAILABLE LOCATION IN FIBLSA
       ENT  FSLIST        START OF FILE SPACE LIST
       ENT  FSLLTH        FILE SPACE LIST LENGTH
       ENT  FSLEND        END OF FILE SPACE LIST
       ENT  ADRFMS        BEGINNING OF FILE MANAGER SPACE ON LIB UNIT
       SPC  2
       EQU  FISLU(LBUNIT) LOGICAL UNIT OF FIS DIRECTORY AND BLOCKS
       EQU  MAXMMA(1)     MAXIMUM NO. OF MASS MEMORY ATTEMPTS ON ERROR
       EQU  FDTPER(1)     FILE/DIRECTORY TIMEOUT PERIOD    (1/10 SEC.)
       EQU  RPTPER(1)     REQUEST PROCESSOR TIMEOUT PERIOD (1/10 SEC.)
       SPC  2
ADRFMS ADC  $5BFB         BEGINNING OF FILE MANAGER SPACE ON LIB UNIT
       SPC  1
********           THE FOLLOWING MUST BE IN ORDER              ********
FIDSEC ADC  0             1. FIS DIRECTORY, S SECTOR ADDRESS
FIBLSA ADC  0             2. SECTOR ADDRESS OF THE LAST FIS BLOCK
FIBNIX ADC  0             3. INDEX TO NEXT AVAILABLE LOCATION IN FIBLSA
FSLIST EQU  FSLIST(*)     4. START OF FILE SPACE LIST
       SPC  1
********           START OF LOGICAL UNIT ENTRIES               ********
       SPC  2
*           L O G I C A L   U N I T   D A T A ,   U N I T   0
*
       ENT NUMFS0
       EQU NUMFS0($1F40)  NUMBER OF FILE SECTORS - UNIT 0
       SPC  1
LUE0   VFD  X9/LUEL0,X7/LBUNIT  LU ENTRY LENGTH(7/15), LOGICAL UNIT(0-6)
       ADC  0                   ADDRESS OF FILE SPACE POOL
       ADC  0                   NUMBER OF AVAILABLE SECTORS
       ADC  NUMFS0              NUMBER OF SECTORS IN THIS FILE SPACE
       NUM  0,1                 THREAD OF ONE SECTOR LONG
       NUM  0,2                 THREAD OF TWO SECTORS LONG
       NUM  0,3                 THREAD OF THREE SECTORS LONG
LUEL0  EQU  LUEL0(*-LUE0)
       EJT
*           L O G I C A L   U N I T   D A T A ,   U N I T   1
*
       ENT BEGLU1
       ENT NUMFS1
       EQU LUNIT1(13)     LOGICAL UNIT OF FILE MANAGER UNIT 1
       EQU BEGLU1(1)      BEGINNING FILE SECTOR  - UNIT 1
       EQU NUMFS1($7FFF)  NUMBER OF FILE SECTORS - UNIT 1
       SPC  1
LUE1   VFD  X9/LUEL1,X7/LUNIT1  LU ENTRY LENGTH(7/15), LOGICAL UNIT(0-6)
       ADC  BEGLU1              ADDRESS OF FILE SPACE POOL
       ADC  0                   NUMBER OF AVAILABLE SECTORS
       ADC  NUMFS1              NUMBER OF SECTORS IN THIS FILE SPACE
       NUM  0,1                 THREAD OF ONE SECTOR LONG
       NUM  0,2                 THREAD OF TWO SECTORS LONG
       NUM  0,3                 THREAD OF THREE SECTORS LONG
LUEL1  EQU  LUEL1(*-LUE1)
       EJT
*           L O G I C A L   U N I T   D A T A ,   U N I T   2
*
       ENT BEGLU2
       ENT NUMFS2
       EQU LUNIT2(14)     LOGICAL UNIT OF FILE MANAGER UNIT 2
       EQU BEGLU2(1)      BEGINNING FILE SECTOR  - UNIT 2
       EQU NUMFS2($7FFF)  NUMBER OF FILE SECTORS - UNIT 2
       SPC  1
LUE2   VFD  X9/LUEL2,X7/LUNIT2  LU ENTRY LENGTH(7/15), LOGICAL UNIT(0-6)
       ADC  BEGLU2              ADDRESS OF FILE SPACE POOL
       ADC  0                   NUMBER OF AVAILABLE SECTORS
       ADC  NUMFS2              NUMBER OF SECTORS IN THIS FILE SPACE
       NUM  0,1                 THREAD OF ONE SECTOR LONG
       NUM  0,2                 THREAD OF TWO SECTORS LONG
       NUM  0,3                 THREAD OF THREE SECTORS LONG
LUEL2  EQU  LUEL2(*-LUE2)
       EJT
*           L O G I C A L   U N I T   D A T A ,   U N I T   3
*
       ENT BEGLU3
       ENT NUMFS3
       EQU LUNIT3(15)     LOGICAL UNIT OF FILE MANAGER UNIT 3
       EQU BEGLU3(1)      BEGINNING FILE SECTOR  - UNIT 3
       EQU NUMFS3($7FFF)  NUMBER OF FILE SECTORS - UNIT 3
       SPC  1
LUE3   VFD  X9/LUEL3,X7/LUNIT3  LU ENTRY LENGTH(7/15), LOGICAL UNIT(0-6)
       ADC  BEGLU3              ADDRESS OF FILE SPACE POOL
       ADC  0                   NUMBER OF AVAILABLE SECTORS
       ADC  NUMFS3              NUMBER OF SECTORS IN THIS FILE SPACE
       NUM  0,1                 THREAD OF ONE SECTOR LONG
       NUM  0,2                 THREAD OF TWO SECTORS LONG
       NUM  0,3                 THREAD OF THREE SECTORS LONG
LUEL3  EQU  LUEL3(*-LUE3)
       EJT
*           L O G I C A L   U N I T   D A T A ,   U N I T   4
*
       ENT BEGLU4
       ENT NUMFS4
       EQU LUNIT4(22)     LOGICAL UNIT OF FILE MANAGER UNIT 4
       EQU BEGLU4(1)      BEGINNING FILE SECTOR  - UNIT 4
       EQU NUMFS4($37F5)  NUMBER OF FILE SECTORS - UNIT 4
       SPC  1
LUE4   VFD  X9/LUEL4,X7/LUNIT4  LU ENTRY LENGTH(7/15), LOGICAL UNIT(0-6)
       ADC  BEGLU4              ADDRESS OF FILE SPACE POOL
       ADC  0                   NUMBER OF AVAILABLE SECTORS
       ADC  NUMFS4              NUMBER OF SECTORS IN THIS FILE SPACE
       NUM  0,1                 THREAD OF ONE SECTOR LONG
       NUM  0,2                 THREAD OF TWO SECTORS LONG
       NUM  0,3                 THREAD OF THREE SECTORS LONG
LUEL4  EQU  LUEL4(*-LUE4)
       EJT
*           L O G I C A L   U N I T   D A T A ,   U N I T   5
*
       ENT BEGLU5
       ENT NUMFS5
       EQU LUNIT5(23)     LOGICAL UNIT OF FILE MANAGER UNIT 5
       EQU BEGLU5(1)      BEGINNING FILE SECTOR  - UNIT 5
       EQU NUMFS5($37F5)  NUMBER OF FILE SECTORS - UNIT 5
       SPC  1
LUE5   VFD  X9/LUEL5,X7/LUNIT5  LU ENTRY LENGTH(7/15), LOGICAL UNIT(0-6)
       ADC  BEGLU5              ADDRESS OF FILE SPACE POOL
       ADC  0                   NUMBER OF AVAILABLE SECTORS
       ADC  NUMFS5              NUMBER OF SECTORS IN THIS FILE SPACE
       NUM  0,1                 THREAD OF ONE SECTOR LONG
       NUM  0,2                 THREAD OF TWO SECTORS LONG
       NUM  0,3                 THREAD OF THREE SECTORS LONG
LUEL5  EQU  LUEL5(*-LUE5)
       EJT
*           L O G I C A L   U N I T   D A T A ,   U N I T   6
*
       ENT BEGLU6
       ENT NUMFS6
       EQU LUNIT6(24)     LOGICAL UNIT OF FILE MANAGER UNIT 6
       EQU BEGLU6(1)      BEGINNING FILE SECTOR  - UNIT 6
       EQU NUMFS6($37F5)  NUMBER OF FILE SECTORS - UNIT 6
       SPC  1
LUE6   VFD  X9/LUEL6,X7/LUNIT6  LU ENTRY LENGTH(7/15), LOGICAL UNIT(0-6)
       ADC  BEGLU6              ADDRESS OF FILE SPACE POOL
       ADC  0                   NUMBER OF AVAILABLE SECTORS
       ADC  NUMFS6              NUMBER OF SECTORS IN THIS FILE SPACE
       NUM  0,1                 THREAD OF ONE SECTOR LONG
       NUM  0,2                 THREAD OF TWO SECTORS LONG
       NUM  0,3                 THREAD OF THREE SECTORS LONG
LUEL6  EQU  LUEL6(*-LUE6)
       EJT
*           L O G I C A L   U N I T   D A T A ,   U N I T   7
*
       ENT BEGLU7
       ENT NUMFS7
       EQU LUNIT7(25)     LOGICAL UNIT OF FILE MANAGER UNIT 7
       EQU BEGLU7(1)      BEGINNING FILE SECTOR  - UNIT 7
       EQU NUMFS7($37F5)  NUMBER OF FILE SECTORS - UNIT 7
       SPC  1
LUE7   VFD  X9/LUEL7,X7/LUNIT7  LU ENTRY LENGTH(7/15), LOGICAL UNIT(0-6)
       ADC  BEGLU7              ADDRESS OF FILE SPACE POOL
       ADC  0                   NUMBER OF AVAILABLE SECTORS
       ADC  NUMFS7              NUMBER OF SECTORS IN THIS FILE SPACE
       NUM  0,1                 THREAD OF ONE SECTOR LONG
       NUM  0,2                 THREAD ON TWO SECTORS LONG
       NUM  0,3                 THREAD OF THREE SECTORS LONG
LUEL7  EQU  LUEL7(*-LUE7)
       EJT
*           L O G I C A L   U N I T   D A T A ,   U N I T   8
*
       ENT BEGLU8
       ENT NUMFS8
       EQU LUNIT8(26)     LOGICAL UNIT OF FILE MANAGER UNIT 8
       EQU BEGLU8(1)      BEGINNING FILE SECTOR  - UNIT 8
       EQU NUMFS8($4000)  NUMBER OF FILE SECTORS - UNIT 8
       SPC  1
LUE8   VFD  X9/LUEL8,X7/LUNIT8  LU ENTRY LENGTH(7/15), LOGICAL UNIT(0-6)
       ADC  BEGLU8              ADDRESS OF FILE SPACE POOL
       ADC  0                   NUMBER OF AVAILABLE SECTORS
       ADC  NUMFS8              NUMBER OF SECTORS IN THIS FILE SPACE
       NUM  0,1                 THREAD OF ONE SECTOR LONG
       NUM  0,2                 THREAD ON TWO SECTORS LONG
       NUM  0,3                 THREAD OF THREE SECTORS LONG
LUEL8  EQU  LUEL8(*-LUE8)
       SPC  2
FSLLTH EQU  FSLLTH(*-FSLIST)    FILE SPACE LIST LENGTH
       SPC  1
FSLEND NUM  -0                  END OF FILE SPACE LIST
       EJT
*           F I L E   M A N A G E R   D A T A
*
       SPC  2
*           LINK UNSELECTED ENTRY POINTS
       SPC  2
*
*           S O R T - M E R G E   L O G I C A L   U N I T S
*
       SPC  1
       ENT  SMCLU1
       ENT  SMCLU2
       ENT  SMCLU3
       ENT  SMCLU4
       SPC  1
SMCLU1 EQU SMCLU1(8)      SORT MERGE L.U. 1
SMCLU2 EQU SMCLU2(8)      SORT MERGE L.U. 2
SMCLU3 EQU SMCLU3(8)      SORT MERGE L.U. 3
SMCLU4 EQU SMCLU4(8)      SORT MERGE L.U. 4
       EJT
*           M I S C E L L A N E O U S   I N F O R M A T I O N
*
*           J O B   P R O C E S S O R   F I L E   P A R A M E T E R S
*
       SPC  3
       ENT  JLLUV4        LOGICAL UNIT OF JOB PROCESSOR FILES
       ENT  JBFLV4        NUMBER OF JOB PROCESSOR FILES
       ENT  FBASV4        FIRST FILE NUMBER USED BY JOB PROCESSOR
       ENT  PKEYV4        JOB FILE PURGE KEY
       SPC  3
JLLUV4 ADC  LBUNIT        LOGICAL UNIT OF JOB PROCESSOR FILES
       SPC  2
       EQU JBFLV4(500)    NUMBER OF JOB PROCESSOR FILES
       SPC  1
       EQU  FBASV4($7F2B-JBFLV4)
*
*      NOTE - FILES $7FFD THRU $7FFF ARE RESERVED FOR THE MSOS
*             VERIFICATION TESTS, FILES $7FF5 THRU $7FFC ARE
*             RESERVED FOR FOREGROUND PSEUDO TAPES, FILES $7FF3
*             THRU $7FF4 ARE RESERVED FOR THE TEXT EDITOR, AND
*             FILES $7F2B THRU $7FF2 ARE RESERVED FOR RPGII.
       SPC  2
       EQU  PKEYV4($3030) JOB FILE PURGE KEY
       EJT
*           P R E S E T   R E G I O N
*
*           PRESET PROTECTED ENTRY POINTS FOR USE BY UNPROTECTED PGMS
*
APRSET EQU  APRSET(*)
       ENT  JPRET
       SPC  2
*           J O B   P R O C E S S O R   P R E S E T
       SPC  2
       EXT  JPRETN
       ALF  3,JPRETN
JPRET  ADC  JPRETN        JOB PROCESSOR RETURN
*
       SPC  2
*           S N A P   D U M P   P R E S E T
       SPC  2
       EXT  SNAPOL
       ALF  3,SNAPOL
       ADC  SNAPOL        REGISTER SNAPSHOT
*
       SPC  2
*           F I L E   M A N A G E R   P R E S E T S
       SPC  2
       EXT  DEFFIL
       ALF  3,DEFFIL
       ADC  DEFFIL        DEFINE FILE
*
       EXT  RELFIL
       ALF  3,RELFIL
       ADC  RELFIL        RELEASE FILE
*
       EXT  DEFIDX
       ALF  3,DEFIDX
       ADC  DEFIDX        DEFINE INDEXED FILE
*
       EXT  LOKFIL
       ALF  3,LOKFIL
       ADC  LOKFIL        LOCK FILE
*
       EXT  UNLFIL
       ALF  3,UNLFIL
       ADC  UNLFIL        UNLOCK FILE
*
       EXT  STOSEQ
       ALF  3,STOSEQ
       ADC  STOSEQ        STORE SEQUENTIAL RECORD
*
       EXT  STODIR
       ALF  3,STODIR
       ADC  STODIR        STORE DIRECT
*
       EXT  STOIDX
       ALF  3,STOIDX
       ADC  STOIDX        STORE INDEXED RECORD
*
       EXT  RTVSEQ
       ALF  3,RTVSEQ
       ADC  RTVSEQ        RETRIEVE SEQUENTIAL RECORD
*
       EXT  RTVDIR
       ALF  3,RTVDIR
       ADC  RTVDIR        RETRIEVE DIRECT
*
       EXT  RTVIDX
       ALF  3,RTVIDX
       ADC  RTVIDX        RETRIEVE INDEXED RECORD
*
       EXT  RTVIDO
       ALF  3,RTVIDO
       ADC  RTVIDO        RETRIEVE INDEXED-ORDERED RECORD
       SPC  2
*           F I L E   M A N A G E R   F L A G   P R E S E T
       SPC  2
       EXT  FMPFLG
       ALF  3,FMPFLG
       ADC  FMPFLG
       SPC  1
LPRSET EQU  LPRSET(*-APRSET)
       EJT
       SPC  10
*           S Y S T E M   L I B R A R Y   D I R E C T O R Y
*
*                         COMPILED FROM *Y, *YM BY SYSTEM INITIALIZER
SLDIRY EQU  SLDIRY(*)
       END
