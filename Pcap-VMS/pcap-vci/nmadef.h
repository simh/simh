/********************************************************************************************************************************/
/* Created: 14-DEC-2002 13:42:17 by OpenVMS SDL EV1-50     */
/* Source:  15-JAN-1990 21:49:30 $1$DKA100:[ANKAN.PCAP.WORK]NMADEF.SDL;1 */
/********************************************************************************************************************************/
/*** MODULE $NMADEF ***/
#pragma __member_alignment __save
#pragma __nomember_alignment
/*                                                                          */
/* Object type                                                              */
/*                                                                          */
#define NMA$C_OBJ_NIC 19                /* Nice listener                    */
/*                                                                          */
/* Function codes                                                           */
/*                                                                          */
#define NMA$C_FNC_LOA 15                /* Request down-line load           */
#define NMA$C_FNC_DUM 16                /* Request up-line dump             */
#define NMA$C_FNC_TRI 17                /* Trigger bootstrap                */
#define NMA$C_FNC_TES 18                /* Test                             */
#define NMA$C_FNC_CHA 19                /* Change parameter                 */
#define NMA$C_FNC_REA 20                /* Read information                 */
#define NMA$C_FNC_ZER 21                /* Zero counters                    */
#define NMA$C_FNC_SYS 22                /* System-specific function         */
/*                                                                          */
/* Option byte                                                              */
/*                                                                          */
/*    common to change parameter, read information and zero counters        */
/*                                                                          */
#define NMA$M_OPT_ENT 0x7
#define NMA$M_OPT_CLE 0x40
#define NMA$M_OPT_PER 0x80
#define NMA$M_OPT_INF 0x70
#define NMA$C_OPINF_SUM 0               /* Summary                          */
#define NMA$C_OPINF_STA 1               /* Status                           */
#define NMA$C_OPINF_CHA 2               /* Characteristics                  */
#define NMA$C_OPINF_COU 3               /* Counters                         */
#define NMA$C_OPINF_EVE 4               /* Events                           */
/*                                                                          */
#define NMA$M_OPT_ACC 0x80
#define NMA$M_OPT_REA 0x80
#define NMA$C_SYS_RST 1                 /* Rsts                             */
#define NMA$C_SYS_RSX 2                 /* Rsx family                       */
#define NMA$C_SYS_TOP 3                 /* Tops-20                          */
#define NMA$C_SYS_VMS 4                 /* Vms                              */
#define NMA$C_SYS_RT 5                  /* RT-11                            */
/*                                                                          */
#define NMA$C_ENT_NOD 0                 /* Node                             */
#define NMA$C_ENT_LIN 1                 /* Line                             */
#define NMA$C_ENT_LOG 2                 /* Logging                          */
#define NMA$C_ENT_CIR 3                 /* Circuit                          */
#define NMA$C_ENT_MOD 4                 /* Module                           */
#define NMA$C_ENT_ARE 5                 /* Area                             */
/*                                                                          */
#define NMA$C_SENT_PROXY 2              /* Proxies                          */
#define NMA$C_SENT_ALI 3                /* Alias                            */
#define NMA$C_SENT_OBJ 4                /* Object                           */
#define NMA$C_SENT_PRO 5                /* Process                          */
#define NMA$C_SENT_SYS 6                /* System                           */
#define NMA$C_SENT_LNK 7                /* Links                            */
#define NMA$C_SENT_WLD -30              /* Wildcarded entity                */
#define NMA$M_ENT_EXE 0x80
#define NMA$C_ENT_WAR -7                /* Wildcarded area                  */
#define NMA$C_ENT_WAD -6                /* Wildcarded address               */
#define NMA$C_ENT_ADJ -4                /* Adjacent                         */
#define NMA$C_ENT_ACT -2                /* Active                           */
#define NMA$C_ENT_KNO -1                /* Known                            */
#define NMA$C_ENT_ADD 0                 /* Node address                     */
#define NMA$C_ENT_ALL -3                /* All                              */
#define NMA$C_ENT_LOO -3                /* Loop                             */
/*                                                                          */
#define NMA$C_SNK_CON 1                 /* Console                          */
#define NMA$C_SNK_FIL 2                 /* File                             */
#define NMA$C_SNK_MON 3                 /* Monitor                          */
/*                                                                          */
#define NMA$M_CNT_TYP 0xFFF
#define NMA$M_CNT_MAP 0x1000
#define NMA$M_CNT_WID 0x6000
#define NMA$M_CNT_COU 0x8000
#define NMA$M_CNT_WIL 0x2000
#define NMA$M_CNT_WIH 0x4000
union NMADEF {
    struct  {
        unsigned NMA$V_OPT_ENT : 3;     /* Entity type                      */
        unsigned NMADEF$$_FILL_1 : 3;
/*                                                                          */
/*    change parameter                                                      */
/*                                                                          */
        unsigned NMA$V_OPT_CLE : 1;     /* Clear parameter                  */
/*                                                                          */
/*    common to change parameter or read information                        */
/*                                                                          */
        unsigned NMA$V_OPT_PER : 1;     /* Permanent parameters             */
        } NMA$R_NMADEF_BITS0;
/*                                                                          */
/*    read information                                                      */
/*                                                                          */
    struct  {
        unsigned NMADEF$$_FILL_2 : 4;
        unsigned NMA$V_OPT_INF : 3;     /* Information type mask            */
        unsigned NMA$V_FILL_0 : 1;
        } NMA$R_NMADEF_BITS1;
/*    test                                                                  */
/*                                                                          */
    struct  {
        unsigned NMADEF$$_FILL_3 : 7;
        unsigned NMA$V_OPT_ACC : 1;     /* Access control included          */
        } NMA$R_NMADEF_BITS2;
/*                                                                          */
/*    zero                                                                  */
/*                                                                          */
    struct  {
        unsigned NMADEF$$_FILL_4 : 7;
        unsigned NMA$V_OPT_REA : 1;     /* Read and zero                    */
        } NMA$R_NMADEF_BITS3;
/*                                                                          */
/* System types                                                             */
/*                                                                          */
/* Entity types.  This numbering scheme must be used in non-system-specific */
/* NICE messages.  (See below for conflicting system-specific entities).    */
/*                                                                          */
/* System-specific (function 22) entity types.  This numbering scheme       */
/* for objects must be used in any entity type in system-specific NICE      */
/* messages.                                                                */
/*                                                                          */
    struct  {
        unsigned NMADEF$$_FILL_5 : 7;
        unsigned NMA$V_ENT_EXE : 1;     /* Executor indicator flag for response messages  */
        } NMA$R_NMADEF_BITS4;
/*                                                                          */
/* Entity identification format types                                       */
/*                                                                          */
/* Logging sink types                                                       */
/*                                                                          */
/* Counter data type values                                                 */
/*                                                                          */
    struct  {
        unsigned NMA$V_CNT_TYP : 12;    /* Type mask                        */
        unsigned NMA$V_CNT_MAP : 1;     /* Bitmapped indicator              */
        unsigned NMA$V_CNT_WID : 2;     /* Width field mask                 */
        unsigned NMA$V_CNT_COU : 1;     /* Counter indicator                */
        } NMA$R_NMADEF_BITS5;
    struct  {
        unsigned NMADEF$$_FILL_6 : 13;
        unsigned NMA$V_CNT_WIL : 1;     /* Width field low bit              */
        unsigned NMA$V_CNT_WIH : 1;     /* Width field high bit             */
        unsigned NMA$V_FILL_1 : 1;
        } NMA$R_NMADEF_BITS6;
/*                                                                          */
/* Node area and address extraction                                         */
/*                                                                          */
    } ;
#define NMA$M_PTY_TYP 0x7FFF
#define NMA$C_PTY_MAX 15                /* Maximum fields within coded multiple  */
#define NMA$M_PTY_CLE 0x3F
#define NMA$M_PTY_MUL 0x40
#define NMA$M_PTY_COD 0x80
#define NMA$M_PTY_CMU 0xC0
#define NMA$M_PTY_NLE 0xF
#define NMA$M_PTY_NTY 0x30
#define NMA$M_PTY_ASC 0x40
#define NMA$C_NTY_DU 0                  /* Unsigned decimal                 */
#define NMA$C_NTY_DS 1                  /* Signed decimal                   */
#define NMA$C_NTY_H 2                   /* Hexidecimal                      */
#define NMA$C_NTY_O 3                   /* Octal                            */
/* NLE values (length of number):                                           */
#define NMA$C_NLE_IMAGE 0               /* Image field (byte-counted)       */
#define NMA$C_NLE_BYTE 1                /* Byte                             */
#define NMA$C_NLE_WORD 2                /* Word                             */
#define NMA$C_NLE_LONG 4                /* Longword                         */
#define NMA$C_NLE_QUAD 8                /* Quadword                         */
/*                                                                          */
#define NMA$C_PTY_AI 64                 /* ASCII image (ASC=1)              */
#define NMA$C_PTY_HI 32                 /* Hex image (NTY=H, NLE=IMAGE)     */
#define NMA$C_PTY_H1 33                 /* Hex byte (NTY=H, NLE=BYTE)       */
#define NMA$C_PTY_H2 34                 /* Hex word (NTY=H, NLE=WORD)       */
#define NMA$C_PTY_H4 36                 /* Hex byte (NTY=H, NLE=LONG)       */
#define NMA$C_PTY_DU1 1                 /* Decimal unsigned byte (NTY=DU,NLE=BYTE)  */
#define NMA$C_PTY_DU2 2                 /* Decimal unsigned word (NTY=DU,NLE=WORD)  */
#define NMA$C_PTY_CD1 129               /* Coded decimal byte (COD=1, 1 byte)  */
#define NMA$C_PTY_CM2 194               /* Coded multiple, 2 fields         */
#define NMA$C_PTY_CM3 195               /* Coded multiple, 3 fields         */
#define NMA$C_PTY_CM4 196               /* Coded multiple, 4 fields         */
#define NMA$C_PTY_CM5 197               /* Coded multiple, 5 fields         */
/*                                                                          */
#define NMA$C_CTLVL_UI 3                /* User interface                   */
#define NMA$C_CTLVL_XID 175             /*                                  */
#define NMA$C_CTLVL_XID_P 191           /*                                  */
#define NMA$C_CTLVL_TEST 227            /*                                  */
#define NMA$C_CTLVL_TEST_P 243          /*                                  */
/*                                                                          */
#define NMA$C_PCCI_STA 0                /* State (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCCI_SUB 1                /* Substate (coded byte of NMA$C_LINSS_)  */
#define NMA$C_PCCI_SER 100              /* Service (coded byte of NMA$C_LINSV_)  */
#define NMA$C_PCCI_LCT 110              /* Counter timer (word)             */
#define NMA$C_PCCI_SPY 120              /* Service physical address (NI address)  */
#define NMA$C_PCCI_SSB 121              /* Service substate (coded byte of NMA$C_LINSS_)  */
#define NMA$C_PCCI_CNO 200              /* Connected node                   */
#define NMA$C_PCCI_COB 201              /* Connected object                 */
#define NMA$C_PCCI_LOO 400              /* Loopback name (ascic)            */
#define NMA$C_PCCI_ADJ 800              /* Adjacent node                    */
#define NMA$C_PCCI_DRT 801              /* Designated router on NI          */
#define NMA$C_PCCI_BLO 810              /* Block size (word)                */
#define NMA$C_PCCI_COS 900              /* Cost (byte)                      */
#define NMA$C_PCCI_MRT 901              /* Maximum routers on NI (byte)     */
#define NMA$C_PCCI_RPR 902              /* Router priority on NI (byte)     */
#define NMA$C_PCCI_HET 906              /* Hello timer (word)               */
#define NMA$C_PCCI_LIT 907              /* Listen timer (word)              */
#define NMA$C_PCCI_BLK 910              /* Blocking (coded byte of NMA$C_CIRBLK_)  */
#define NMA$C_PCCI_MRC 920              /* Maximum recalls (byte)           */
#define NMA$C_PCCI_RCT 921              /* Recall timer (word)              */
#define NMA$C_PCCI_NUM 930              /* Number (ascic)                   */
#define NMA$C_PCCI_USR 1000             /* User entity identification       */
#define NMA$C_PCCI_POL 1010             /* Polling state (coded byte of NMA$C_CIRPST_)  */
#define NMA$C_PCCI_PLS 1011             /* Polling substate (coded byte)    */
#define NMA$C_PCCI_OWN 1100             /* Owner entity identification      */
#define NMA$C_PCCI_LIN 1110             /* Line (ascic)                     */
#define NMA$C_PCCI_USE 1111             /* Usage (coded byte of NMA$C_CIRUS_)  */
#define NMA$C_PCCI_TYP 1112             /* Type (coded byte of NMA$C_CIRTY_)  */
#define nma$c_pcci_net 1119             /* Network (ascic)                  */
#define NMA$C_PCCI_DTE 1120             /* DTE (ascic)                      */
#define NMA$C_PCCI_CHN 1121             /* Channel (word)                   */
#define NMA$C_PCCI_MBL 1122             /* Maximum data (word)              */
#define NMA$C_PCCI_MWI 1123             /* Maximum window (byte)            */
#define NMA$C_PCCI_TRI 1140             /* Tributary (byte)                 */
#define NMA$C_PCCI_BBT 1141             /* Babble timer (word)              */
#define NMA$C_PCCI_TRT 1142             /* Transmit timer (word)            */
#define NMA$C_PCCI_RTT 1143             /* Retransmit timer (word)          */
#define NMA$C_PCCI_MRB 1145             /* Maximum receive buffers (coded byte)  */
/* 0-254 is value, 255 = UNLIMITED                                          */
#define NMA$C_PCCI_MTR 1146             /* Maximum transmits (byte)         */
#define NMA$C_PCCI_ACB 1150             /* Active base (byte)               */
#define NMA$C_PCCI_ACI 1151             /* Active increment (byte)          */
#define NMA$C_PCCI_IAB 1152             /* Inactive base (byte)             */
#define NMA$C_PCCI_IAI 1153             /* Inactive increment (byte)        */
#define NMA$C_PCCI_IAT 1154             /* Inactive threshold (byte)        */
#define NMA$C_PCCI_DYB 1155             /* Dying base (byte)                */
#define NMA$C_PCCI_DYI 1156             /* Dying increment (byte)           */
#define NMA$C_PCCI_DYT 1157             /* Dying threshold (byte)           */
#define NMA$C_PCCI_DTH 1158             /* Dead threshold (byte)            */
/*                                                                          */
#define NMA$C_PCCI_RSX_MAC 2320         /* Multipoint active ratio          */
#define NMA$C_PCCI_RSX_LOG 2380         /* Logical name                     */
#define NMA$C_PCCI_RSX_DLG 2385         /* Designated name                  */
#define NMA$C_PCCI_RSX_ACT 2390         /* Actual name                      */
/*                                                                          */
#define NMA$C_PCCI_VER 2700             /* Verification (coded byte of NMA$C_CIRVE_)  */
#define NMA$C_PCCI_XPT 2720             /* Transport type (coded byte of NMA$C_CIRXPT_)  */
/*  VMS Specific codes that are used for the X21 project                    */
#define NMA$C_PCCI_IRC 2750             /*Incoming Reverse                  */
#define NMA$C_PCCI_ORC 2751             /*Outgoing Reverse                  */
#define NMA$C_PCCI_GRP 2752             /*Cug                               */
#define NMA$C_PCCI_NOP 2753             /*National Facility Data            */
#define NMA$C_PCCI_CAL 2754             /*Call request "Now/Clear"          */
#define NMA$C_PCCI_INA 2755             /*Inactive                          */
#define NMA$C_PCCI_RED 2756             /*Redirected status                 */
#define NMA$C_PCCI_MOD 2757             /*Time-cut Mode status "Auto/Noauto" */
#define NMA$C_PCCI_REQ 2758             /*Request timer T1                  */
#define NMA$C_PCCI_DTW 2759             /*Dte waiting timer t2              */
#define NMA$C_PCCI_PRO 2760             /*Progress timer t3a                */
#define NMA$C_PCCI_INF 2761             /*Information timer t4a             */
#define NMA$C_PCCI_ACC 2762             /*Accepted timer t4b                */
#define NMA$C_PCCI_CLR 2763             /*Request timer t5                  */
#define NMA$C_PCCI_DTC 2764             /*Dte clear timer t6                */
#define NMA$C_PCCI_CCG 2765             /*Charging timer t7                 */
#define NMA$C_PCCI_ESA 2766             /*Enhanced Subaddress               */
#define NMA$C_PCCI_DTI 2767             /*DTE provided info                 */
#define NMA$C_PCCI_SWC 2768             /*Switched - set line leased        */
#define NMA$C_PCCI_TIC 2769             /*Timecutting on/off                */
#define NMA$C_PCCI_CSG 2770             /*Send signal-data enable/disable   */
#define NMA$C_PCCI_AAS 2771             /*Abbreviated address.              */
#define NMA$C_PCCI_DTS 2772             /*DTE Status                        */
#define NMA$C_PCCI_CAS 2773             /*Call Status                       */
#define NMA$C_PCCI_CPS 2774             /*Call-Progress Status              */
#define NMA$C_PCCI_CNT 2775             /*Counter .                         */
#define NMA$C_PCCI_RAT 2776             /*Rate item read only for show      */
#define NMA$C_PCCI_PRD 2777             /*Period hh:mm-hh:mm                */
#define NMA$C_PCCI_DAY 2778             /*day of week                       */
#define NMA$C_PCCI_BFN 2779             /*number of buffers for driver to issue */
#define NMA$C_PCCI_BSZ 2780             /*size of buffer to allocate.       */
#define NMA$C_PCCI_MDM 2781             /*Modem signals on/off              */
#define NMA$C_PCCI_DTL 2782             /*DTE-List element.                 */
#define NMA$C_PCCI_IDL 2783             /*Idle time                         */
#define NMA$C_PCCI_IMT 2784             /*Initial Minimum timer             */
#define NMA$C_PCCI_CAC 2785             /*Call accept control               */
#define NMA$C_PCCI_ORD 2786             /*Outgoing request Delay            */
#define NMA$C_PCCI_CID 2787             /*Calling DTE id                    */
/*                                                                          */
#define NMA$C_PCCI_MST 2810             /* Maintenance state                */
/*                                                                          */
#define NMA$C_PCCI_SRV_LOG 3380         /* Logical name                     */
#define NMA$C_PCCI_SRV_DLG 3385         /* Designated name                  */
#define NMA$C_PCCI_SRV_ACT 3390         /* Actual name                      */
/*                                                                          */
#define NMA$C_PCLI_STA 0                /* State (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCLI_SUB 1                /* Substate (coded byte of NMA$C_LINSS_)  */
#define NMA$C_PCLI_SER 100              /* Service (coded byte of NMA$C_LINSV_)  */
#define NMA$C_PCLI_LCT 110              /* Counter timer (word)             */
#define NMA$C_PCLI_LOO 400              /* Loopback name (ascic) [V2 only]  */
#define NMA$C_PCLI_ADJ 800              /* Adjacent node [V2 only]          */
#define NMA$C_PCLI_BLO 810              /* Block size (word) [V2 only]      */
#define NMA$C_PCLI_COS 900              /* Cost (byte) [V2 only]            */
#define NMA$C_PCLI_DEV 1100             /* Device (ascic)                   */
#define NMA$C_PCLI_BFN 1105             /* Receive buffers                  */
#define NMA$C_PCLI_CON 1110             /* Controller (coded byte of NMA$C_LINCN_)  */
#define NMA$C_PCLI_DUP 1111             /* Duplex (coded byte of NMA$C_DPX_)  */
#define NMA$C_PCLI_PRO 1112             /* Protocol (coded byte of NMA$C_LINPR_)  */
#define NMA$C_PCLI_LTY 1112             /* Type (coded byte of NMA$C_LINTY_) [V2 only]  */
#define NMA$C_PCLI_CLO 1113             /* Clock (coded byte of NMA$C_LINCL_)  */
#define NMA$C_PCLI_STI 1120             /* Service timer (word)             */
#define NMA$C_PCLI_NTI 1121             /* Normal timer (word) [V2 only]    */
#define NMA$C_PCLI_RTT 1121             /* Retransmit timer (word)          */
#define NMA$C_PCLI_HTI 1122             /* Holdback timer (word)            */
#define NMA$C_PCLI_MBL 1130             /* Maximum block (word)             */
#define NMA$C_PCLI_MRT 1131             /* Maximum retransmits (byte)       */
#define NMA$C_PCLI_MWI 1132             /* Maximum window (byte)            */
#define NMA$C_PCLI_TRI 1140             /* Tributary (byte) [V2 only]       */
#define NMA$C_PCLI_SLT 1150             /* Scheduling timer (word)          */
#define NMA$C_PCLI_DDT 1151             /* Dead timer (word)                */
#define NMA$C_PCLI_DLT 1152             /* Delay timer (word)               */
#define NMA$C_PCLI_SRT 1153             /* Stream timer (word)              */
#define NMA$C_PCLI_HWA 1160             /* Hardware address (NI address)    */
#define nma$c_pcli_net 1190             /* Network name (ascic)             */
#define NMA$C_PCLI_XMD 1191             /* X.25 line mode (coded byte of NMA$C_X25MD_)  */
/*                                                                          */
#define NMA$C_PCLI_RSX_OWN 2300         /* Owner                            */
#define NMA$C_PCLI_RSX_CCS 2310         /* Controller CSR                   */
#define NMA$C_PCLI_RSX_UCS 2311         /* Unit CSR                         */
#define NMA$C_PCLI_RSX_VEC 2312         /* Vector                           */
#define NMA$C_PCLI_RSX_PRI 2313         /* Priority                         */
#define NMA$C_PCLI_RSX_MDE 2321         /* Dead polling ratio               */
#define NMA$C_PCLI_RSX_LLO 2330         /* Location                         */
/*  0, Firstfit                                                             */
#define NMA$C_PCLI_RSX_LOG 2380         /* Logical name                     */
#define NMA$C_PCLI_RSX_DLG 2385         /* Designated name                  */
#define NMA$C_PCLI_RSX_ACT 2390         /* Actual name                      */
/*                                                                          */
#define NMA$C_PCLI_MCD 2701             /* Micro-code dump filespec (ascic)  */
#define NMA$C_PCLI_EPT 2720             /* Ethernet Protocol Type (hex word)  */
#define NMA$C_PCLI_LNS 2730             /* Line speed (word)                */
#define NMA$C_PCLI_SWI 2740             /* SWITCH (coded byte of nma$c_linswi_) */
#define NMA$C_PCLI_HNG 2750             /* HANGUP (coded byte of NMA$C_LINHNG_) */
#define NMA$C_PCLI_TPI 2760             /* Transmit pipeline                */
#define nma$c_pcli_nrzi 2761            /* NRZI bit encoding                */
#define nma$c_pcli_code 2762            /* Character code (encoded as CODE_) */
/*   This section are parameters for 802 support.                           */
#define NMA$C_PCLI_FMT 2770             /* Packet format(coded of linfm_)   */
#define NMA$C_PCLI_SRV 2771             /* Driver service coded of linsr    */
#define NMA$C_PCLI_SAP 2772             /* SAP                              */
#define NMA$C_PCLI_GSP 2773             /* GSP                              */
#define NMA$C_PCLI_PID 2774             /* PID                              */
#define NMA$C_PCLI_CNM 2775             /* Client name                      */
#define NMA$C_PCLI_CCA 2776             /* Can change address               */
#define NMA$C_PCLI_APC 2777             /* Allow promiscuous client         */
#define NMA$C_PCLI_MED 2778             /* Communication medium             */
#define NMA$C_PCLI_PNM 2779             /* Port name                        */
#define NMA$C_PCLI_SNM 2780             /* Station name                     */
/*                                                                          */
#define NMA$C_PCLI_BUS 2801             /* Buffer size (word)               */
#define NMA$C_PCLI_NMS 2810             /* Number of DMP/DMF synch chars (word)  */
#define NMA$C_PCLI_PHA 2820             /* Physical NI address of UNA (hex string)  */
#define NMA$C_PCLI_DPA 2821             /* (same as HWA) ; Default UNA physical address (hex string)  */
#define NMA$C_PCLI_PTY 2830             /* Ethernet Protocol type (word)    */
#define NMA$C_PCLI_MCA 2831             /* UNA Multicast address list (special)  */
/*        (See NMA$C_LINMC_)                                                */
#define NMA$C_PCLI_ILP 2839             /* DELUA Internal Loopback mode     */
/* (coded byte of NMA$C_STATE_)                                             */
#define NMA$C_PCLI_PRM 2840             /* UNA Promiscuous mode (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCLI_MLT 2841             /* UNA Multicast address mode (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCLI_PAD 2842             /* UNA Padding mode (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCLI_DCH 2843             /* UNA Data chaining mode (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCLI_CRC 2844             /* UNA CRC mode (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCLI_HBQ 2845             /* UNA Hardware Buffer Quota (word)  */
#define NMA$C_PCLI_ACC 2846             /* UNA protocol access mode (coded byte of NMA$C_ACC_)  */
#define NMA$C_PCLI_EKO 2847             /* UNA Echo mode (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCLI_BSZ 2848             /* UNA Device Buffer size           */
#define NMA$C_PCLI_DES 2849             /* UNA destination Ethernet address  */
#define NMA$C_PCLI_RET 2850             /* PCL number of retries (word)     */
#define NMA$C_PCLI_MOD 2851             /* PCL address mode (coded byte of NMA$C_LINMO_)  */
#define NMA$C_PCLI_RIB 2852             /* PCL retry-if-busy state (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCLI_MNTL 2860            /* Maintenance loopback mode for devices  */
/*  which support several different loop back modes                         */
#define NMA$C_PCLI_INTL0 2861           /* Internal loopback level 0        */
#define NMA$C_PCLI_INTL1 2862           /* Internal loopback level 1        */
#define NMA$C_PCLI_INTL2 2863           /* Internal loopback level 2        */
#define NMA$C_PCLI_INTL3 2864           /* Internal loopback level 3        */
#define NMA$C_PCLI_FRA 2865             /* Framing address for Bisync       */
#define NMA$C_PCLI_STI1 2866            /* State info 1st longword          */
#define NMA$C_PCLI_STI2 2867            /* State info 2st longword          */
#define NMA$C_PCLI_TMO 2868             /* Wait for CTS time out value for DMF sync half duplex  */
#define NMA$C_PCLI_MCL 2869             /* Clear modem on deassign of channel  */
#define NMA$C_PCLI_SYC 2870             /* BISYNC protocol sync char        */
#define NMA$C_PCLI_BPC 2871             /* Number of bits per character     */
#define NMA$C_PCLI_MBS 2872             /* Maximum buffer size              */
#define NMA$C_PCLI_RES 2873             /* Restart valuse (coded byte of LINRES_ */
/*                                                                          */
#define NMA$C_PCLI_SRV_OWN 3300         /* Owner                            */
#define NMA$C_PCLI_SRV_UCS 3311         /* Unit CSR                         */
#define NMA$C_PCLI_SRV_VEC 3312         /* Vector                           */
#define NMA$C_PCLI_SRV_PRI 3313         /* Priority                         */
#define NMA$C_PCLI_SRV_LOG 3380         /* Logical name                     */
#define NMA$C_PCLI_SRV_DLG 3385         /* Designated name                  */
#define NMA$C_PCLI_SRV_ACT 3390         /* Actual name                      */
/*                                                                          */
#define NMA$C_LINMD_CSMACD 10           /*                                  */
#define NMA$C_LINMD_FDDI 11             /*                                  */
#define NMA$C_LINMD_CI 12               /*                                  */
/*                                                                          */
#define NMA$C_PCCO_RTR 110              /* Reservation timer (word)         */
/*                                                                          */
#define NMA$C_PCLD_ASS 10               /* Assistance flag (coded byte of NMA$C_ASS_)  */
/*                                                                          */
#define NMA$C_PCLP_ASS 10               /* Assistance flag (coded byte of NMA$C_ASS_)  */
/*                                                                          */
#define NMA$C_PCCN_CIR 100              /* NI circuit name (ascic)          */
#define NMA$C_PCCN_SUR 110              /* Surveillance flag (coded byte of NMA$C_SUR_)  */
#define NMA$C_PCCN_ELT 111              /* Elapsed time                     */
#define NMA$C_PCCN_PHA 120              /* Physical address (NI address)    */
#define NMA$C_PCCN_LRP 130              /* Time of last report              */
#define NMA$C_PCCN_MVR 20001            /* Maintenance version              */
#define NMA$C_PCCN_FCT 20002            /* Function list                    */
#define NMA$C_PCCN_CUS 20003            /* Current console user (NI address)  */
#define NMA$C_PCCN_RTR 20004            /* Reservation timer (word)         */
#define NMA$C_PCCN_CSZ 20005            /* Command buffer size (word)       */
#define NMA$C_PCCN_RSZ 20006            /* Response buffer size (word)      */
#define NMA$C_PCCN_HWA 20007            /* Hardware address (NI address)    */
#define NMA$C_PCCN_DTY 20100            /* Device type (coded byte of NMA$C_SOFD_)  */
#define NMA$C_PCCN_SFI 20200            /* Software ID                      */
#define NMA$C_PCCN_SPR 20300            /* System processor (coded word)    */
#define NMA$C_PCCN_DLK 20400            /* Data link type (coded word)      */
/*                                                                          */
#define NMA$C_PCLO_STA 0                /* State (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCLO_LNA 100              /* System/name (ascic)              */
#define NMA$C_PCLO_SIN 200              /* Sink node                        */
#define NMA$C_PCLO_EVE 201              /* Events                           */
/*                                                                          */
#define NMA$C_PCXA_NOD 320              /* Node                             */
#define NMA$C_PCXA_USR 330              /* User (ascic)                     */
#define NMA$C_PCXA_SPW 331              /* Password to set (ascic)          */
#define NMA$C_PCXA_RPW 331              /* Password to read (coded byte of NMA$C_NODPW_)  */
#define NMA$C_PCXA_ACC 332              /* Account (ascic)                  */
#define NMA$C_PCXA_NET 1110             /* Network (ascic)                  */
/*                                                                          */
#define NMA$C_PCXA_RSX_ADS 2310         /* Destination                      */
#define NMA$C_PCXA_RSX_ANB 2320         /* Number                           */
#define NMA$C_PCXA_RSX_ASC 2330         /* Scope                            */
/*                                                                          */
#define NMA$C_PCXA_SRV_ADS 3310         /* Destination                      */
#define NMA$C_PCXA_SRV_ANB 3320         /* Number                           */
#define NMA$C_PCXA_SRV_ASC 3330         /* Scope                            */
/*                                                                          */
#define NMA$C_PCXP_STA 0                /* State (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCXP_SBS 1                /* Substate, qualified by DTE (coded byte of NMA$C_XPRSB_)  */
#define NMA$C_PCXP_CTM 100              /* Counter timer (word)             */
#define NMA$C_PCXP_ACH 1000             /* Active channels (word)           */
#define NMA$C_PCXP_ASW 1010             /* Active switched (word)           */
#define NMA$C_PCXP_DTE 1100             /* DTE (ascic)                      */
#define NMA$C_PCXP_GRP 1101             /* Group (ascic)                    */
#define NMA$C_pcxp_netent 1110          /* Network entity (ascic)           */
#define NMA$C_pcxp_dnt 1111             /* DTE Network (ascic)              */
#define NMA$C_PCXP_LIN 1120             /* Line (ascic)                     */
#define NMA$C_PCXP_CHN 1130             /* Channels                         */
#define NMA$C_PCXP_MCH 1131             /* Maximum channels (word)          */
#define NMA$C_PCXP_DBL 1140             /* Default data (word)              */
#define NMA$C_PCXP_DWI 1141             /* Default window (byte)            */
#define NMA$C_PCXP_MBL 1150             /* Maximum data (word)              */
#define NMA$C_PCXP_MWI 1151             /* Maximum window (byte)            */
#define NMA$C_PCXP_MCL 1152             /* Maximum clears (byte)            */
#define NMA$C_PCXP_MRS 1153             /* Maximum resets (byte)            */
#define NMA$C_PCXP_MST 1154             /* Maximum restarts (byte)          */
#define NMA$C_PCXP_CAT 1160             /* Call timer (byte)                */
#define NMA$C_PCXP_CLT 1161             /* Clear timer (byte)               */
#define NMA$C_PCXP_RST 1162             /* Reset timer (byte)               */
#define NMA$C_PCXP_STT 1163             /* Restart timer (byte)             */
#define NMA$C_pcxp_itt 1164             /* Interrupt timer (byte)           */
#define NMA$C_PCXP_GDT 1170             /* Group DTE (ascic)                */
#define NMA$C_PCXP_GNM 1171             /* Group number (word)              */
#define NMA$C_PCXP_GTY 1172             /* Group type (coded byte of NMA$C_XPRTY_)  */
#define NMA$C_pcxp_gnt 1173             /* Group Network name (ascic)       */
#define nma$c_pcxp_mode 1180            /* DTE mode (coded byte of NMA$C_X25MD_) */
#define nma$c_pcxp_prof 1190            /* Profile (ascic)                  */
/*                                                                          */
#define NMA$C_PCXP_RSX_PMC 2300         /* Maximum circuits                 */
/*                                                                          */
#define NMA$C_PCXP_MCI 2710             /* Maximum circuits, qualified by DTE  */
/*                                                                          */
#define NMA$C_PCXP_SRV_PMC 3300         /* Maximum circuits                 */
/*                                                                          */
#define nma$c_pcxs_sta 1                /* State (coded byte of NMA$C_STATE_) */
#define NMA$C_PCXS_CTM 100              /* Counter timer (word)             */
#define NMA$C_PCXS_ACI 200              /* Active circuits (word)           */
#define NMA$C_PCXS_DST 300              /* Destination (ascic)              */
#define NMA$C_PCXS_MCI 310              /* Maximum circuits (word)          */
#define NMA$C_PCXS_NOD 320              /* Node                             */
#define NMA$C_PCXS_USR 330              /* Username                         */
#define NMA$C_PCXS_SPW 331              /* Password to set (ascic)          */
#define NMA$C_PCXS_RPW 331              /* Password to read (coded byte of NMA$C_NODPW_)  */
#define NMA$C_PCXS_ACC 332              /* Account (ascic)                  */
#define NMA$C_PCXS_OBJ 340              /* Object                           */
#define NMA$C_PCXS_PRI 350              /* Priority (byte)                  */
#define NMA$C_PCXS_CMK 351              /* Call mask (byte-counted hex)     */
#define NMA$C_PCXS_CVL 352              /* Call value (byte-counted hex)    */
#define NMA$C_PCXS_GRP 353              /* Group (ascic)                    */
#define NMA$C_PCXS_SDTE 354             /* Sending DTE, formally "Number" (ascic)  */
#define NMA$C_PCXS_SAD 355              /* Subaddresses                     */
#define nma$c_pcxs_red 390              /* Redirect reason (coded byte nma$c_x25red_) */
#define nma$c_pcxs_cdte 391             /* Called DTE (ascic)               */
#define nma$c_pcxs_rdte 392             /* Receiving DTE (ascic)            */
#define nma$c_pcxs_net 393              /* Network (ascic)                  */
#define nma$C_pcxs_emk 394              /* Extension mask (ascic)           */
#define nma$C_pcxs_evl 395              /* Extension value (ascic)          */
#define nma$C_pcxs_idte 396             /* Incoming address (ascii)         */
/*                                                                          */
#define NMA$C_PCXS_RSX_5ST 2310         /* State                            */
/*  0, On                                                                   */
#define NMA$C_PCXS_FIL 2710             /* Object filespec (ascic)          */
/*                                                                          */
#define NMA$C_PCXS_SRV_5ST 3310         /* State                            */
/*  0, On                                                                   */
#define NMA$C_PCXT_STA 0                /* State (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCXT_BSZ 100              /* Buffer size (word)               */
#define NMA$C_PCXT_MBK 101              /* Maximum blocks/file (word)       */
#define NMA$C_PCXT_FNM 102              /* Filename (ascic)                 */
#define NMA$C_PCXT_MBF 103              /* Maximum number of buffers (word)  */
#define NMA$C_PCXT_CPL 104              /* Global data capture limit (word)  */
#define NMA$C_PCXT_MVR 105              /* Maximum trace file version (word)  */
#define NMA$C_PCXT_TPT 106              /* Trace point name (ascic)         */
#define NMA$C_PCXT_CPS 110              /* Per-trace capture size (word)    */
#define NMA$C_PCXT_TST 111              /* Per-trace state (coded byte of NMA$C_STATE_)  */
/*                                                                          */
#define NMA$C_PCNO_STA 0                /* State (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCNO_PHA 10               /* Physical address (NI address)    */
#define NMA$C_PCNO_IDE 100              /* Identification (ascic)           */
#define NMA$C_PCNO_MVE 101              /* Management version (3 bytes)     */
#define NMA$C_PCNO_SLI 110              /* Service circuit (ascic)          */
#define NMA$C_PCNO_SPA 111              /* Service password (8 bytes)       */
#define NMA$C_PCNO_SDV 112              /* Service device (coded byte of NMA$C_SOFD_)  */
#define NMA$C_PCNO_CPU 113              /* CPU type (coded byte of NMA$C_CPU_)  */
#define NMA$C_PCNO_HWA 114              /* Hardware address (NI address)    */
#define NMA$C_PCNO_SNV 115              /* Service node version (coded byte of NMA$C_SVN_)  */
#define NMA$C_PCNO_LOA 120              /* Load file (ascic)                */
#define NMA$C_PCNO_SLO 121              /* Secondary loader (ascic)         */
#define NMA$C_PCNO_TLO 122              /* Tertiary loader (ascic)          */
#define NMA$C_PCNO_DFL 123              /* Diagnostic file (ascic)          */
#define NMA$C_PCNO_STY 125              /* Software type (coded byte of NMA$C_SOFT_)  */
#define NMA$C_PCNO_SID 126              /* Software ID (ascic)              */
#define NMA$C_PCNO_MFL 127              /* Management File (ascic)          */
#define NMA$C_PCNO_DUM 130              /* Dump file (ascic)                */
#define NMA$C_PCNO_SDU 131              /* Secondary dumper (ascic)         */
#define NMA$C_PCNO_DAD 135              /* Dump address (longword)          */
#define NMA$C_PCNO_DCT 136              /* Dump count (longword)            */
#define NMA$C_PCNO_OHO 140              /* Host (read only parameter)       */
#define NMA$C_PCNO_IHO 141              /* Host (write only parameter)      */
#define NMA$C_PCNO_LPC 150              /* Loop count (word)                */
#define NMA$C_PCNO_LPL 151              /* Loop length (word)               */
#define NMA$C_PCNO_LPD 152              /* Loop Data type (coded byte of NMA$C_LOOP_)  */
#define NMA$C_PCNO_LPA 153              /* Loop assistant physical address (NI address)  */
#define NMA$C_PCNO_LPH 154              /* Loop help type (coded byte)      */
#define NMA$C_PCNO_LPN 155              /* Loop circuit node                */
#define NMA$C_PCNO_LAN 156              /* Loop circuit assistant node      */
#define NMA$C_PCNO_CTI 160              /* Counter timer (word)             */
#define NMA$C_PCNO_NNA 500              /* Name                             */
#define NMA$C_PCNO_NLI 501              /* Circuit (ascic)                  */
#define NMA$C_PCNO_ADD 502              /* Address                          */
#define NMA$C_PCNO_ITI 510              /* Incoming timer (word)            */
#define NMA$C_PCNO_OTI 511              /* Outgoing timer (word)            */
#define NMA$C_PCNO_IPR 522              /* Incoming Proxy                   */
#define NMA$C_PCNO_OPR 523              /* Outgoing Proxy                   */
#define NMA$C_PCNO_ACL 600              /* Active links (word)              */
#define NMA$C_PCNO_DEL 601              /* Delay (word)                     */
#define NMA$C_PCNO_NVE 700              /* Nsp version (3 bytes)            */
#define NMA$C_PCNO_MLK 710              /* Maximum links (word)             */
#define NMA$C_PCNO_DFA 720              /* Delay factor (byte)              */
#define NMA$C_PCNO_DWE 721              /* Delay weight (byte)              */
#define NMA$C_PCNO_IAT 722              /* Inactivity timer (word)          */
#define NMA$C_PCNO_RFA 723              /* Retransmit factor (word)         */
#define NMA$C_PCNO_DTY 810              /* Destination Type (coded byte of NMA$C_XPRTY_)  */
#define NMA$C_PCNO_DCO 820              /* Destination Cost (word)          */
#define NMA$C_PCNO_DHO 821              /* Destination Hops (byte)          */
#define NMA$C_PCNO_DLI 822              /* Destination circuit (ascic)      */
#define NMA$C_PCNO_NND 830              /* Next node to destination         */
#define NMA$C_PCNO_RVE 900              /* Routing version (3 bytes)        */
#define NMA$C_PCNO_ETY 901              /* Executor Type (coded byte of NMA$C_NODTY_)  */
#define NMA$C_PCNO_RTI 910              /* Routing timer (word)             */
#define NMA$C_PCNO_SAD 911              /* Subaddress (2 words)             */
#define NMA$C_PCNO_BRT 912              /* Broadcast routing timer (word)   */
#define NMA$C_PCNO_MAD 920              /* Maximum address (word)           */
#define NMA$C_PCNO_MLN 921              /* Maximum circuits (word)          */
#define NMA$C_PCNO_MCO 922              /* Maximum cost (word)              */
#define NMA$C_PCNO_MHO 923              /* Maximum hops (byte)              */
#define NMA$C_PCNO_MVI 924              /* Maximum visits (byte)            */
#define NMA$C_PCNO_MAR 925              /* Maximum areas (byte)             */
#define NMA$C_PCNO_MBE 926              /* Maximum broadcast nonrouters (word)  */
#define NMA$C_PCNO_MBR 927              /* Maximum broadcast routers (word)  */
#define NMA$C_PCNO_AMC 928              /* Area maximum cost (word)         */
#define NMA$C_PCNO_AMH 929              /* Area maximum hops (byte)         */
#define NMA$C_PCNO_MBU 930              /* Maximum buffers (word)           */
#define NMA$C_PCNO_BUS 931              /* Executor buffer size (word)      */
#define NMA$C_PCNO_SBS 932              /* Segment buffer size (word)       */
#define NMA$C_PCNO_MPS 933              /* Maximum path splits              */
#define NMA$C_PCNO_FBS 933              /* Forwarding buffer size (word)    */
/*                                                                          */
#define NMA$C_PCNO_RSX_RPA 2300         /* Receive password                 */
/*  0, Password set                                                         */
#define NMA$C_PCNO_RSX_TPA 2301         /* Transmit password                */
/*  0, Password set                                                         */
#define NMA$C_PCNO_RSX_VER 2310         /* Verification state               */
/*  0, On                                                                   */
#define NMA$C_PCNO_PUS 2704             /* Privileged user id               */
#define NMA$C_PCNO_PAC 2705             /* Privileged account               */
#define NMA$C_PCNO_PPW 2706             /* Privileged password              */
#define NMA$C_PCNO_NUS 2712             /* Non-privileged user id           */
#define NMA$C_PCNO_NAC 2713             /* Non-privileged account           */
#define NMA$C_PCNO_NPW 2714             /* Non-privileged password          */
#define NMA$C_PCNO_RPA 2720             /* Receive password                 */
#define NMA$C_PCNO_TPA 2721             /* Transmit password                */
#define NMA$C_PCNO_ACC 2730             /* Access (coded byte of NMA$C_ACES_)  */
#define NMA$C_PCNO_DAC 2731             /* Default access (coded byte of NMA$C_ACES_)  */
#define NMA$C_PCNO_PIQ 2740             /* Pipeline quota (word)            */
#define NMA$C_PCNO_ALI 2742             /* Alias incoming (coded byte of ALIINC)) */
#define NMA$C_PCNO_ALM 2743             /* Alias Maximum links              */
#define NMA$C_PCNO_ALN 2744             /* Alias node                       */
#define NMA$C_PCNO_PRX 2750             /* Proxy access (coded byte of NMA$C_ACES_) !! Obsolete: Only for LIST/PURGE  */
#define NMA$C_PCNO_DPX 2751             /* Default proxy access (coded byte of NMA$C_ACES_)  */
#define NMA$C_PCNO_COP 2760             /* Remote nodefor COPY command      */
#define NMA$C_PCNO_INB 2765             /* Inbound for async DECnet.        */
#define NMA$C_PCNO_LAA 2770             /* Load Assist Agent                */
#define NMA$C_PCNO_LAP 2771             /* Load Assist Parameter            */
#define NMA$C_PCNO_PSP 2780             /* Path Splits Policy               */
/* (Coded byte f PSPCY)                                                     */
#define NMA$C_PCNO_MDO 2785             /* Maximum Declared Objects         */
/*                                                                          */
#define NMA$C_PCNO_SRV_RPA 3300         /* Receive password                 */
/*  0, Password set                                                         */
#define NMA$C_PCNO_SRV_TPA 3301         /* Transmit password                */
/*  0, Password set                                                         */
#define NMA$C_PCNO_SRV_VER 3310         /* Verification state               */
/*  0, On                                                                   */
#define NMA$C_PCNO_SRV_ACB 3402         /* Active control buffers           */
#define NMA$C_PCNO_SRV_ASB 3404         /* Active small buffers             */
#define NMA$C_PCNO_SRV_ALB 3406         /* Active large buffers             */
#define NMA$C_PCNO_SRV_MCB 3410         /* Maximum control buffers          */
#define NMA$C_PCNO_SRV_MSB 3420         /* Maximum small buffers            */
#define NMA$C_PCNO_SRV_MLB 3430         /* Maximum large buffers            */
#define NMA$C_PCNO_SRV_LBS 3431         /* Large buffer size                */
#define NMA$C_PCNO_SRV_NRB 3440         /* Minimum receive buffers          */
#define NMA$C_PCNO_SRV_CPT 3450         /* CEX pool: total bytes            */
#define NMA$C_PCNO_SRV_CPF 3452         /* CEX pool: number of segments     */
#define NMA$C_PCNO_SRV_CPL 3454         /* CEX pool: largest segment        */
#define NMA$C_PCNO_SRV_XPT 3460         /* Extended pool: total bytes       */
#define NMA$C_PCNO_SRV_XPF 3462         /* Extended pool: number of segments  */
#define NMA$C_PCNO_SRV_XPL 3464         /* Extended pool: largest segment   */
/*                                                                          */
#define NMA$C_PCAR_STA 0                /* State (coded byte of NMA$C_STATE_)  */
#define NMA$C_PCAR_COS 820              /* Cost (word)                      */
#define NMA$C_PCAR_HOP 821              /* Hops (byte)                      */
#define NMA$C_PCAR_CIR 822              /* Circuit (ascic)                  */
#define NMA$C_PCAR_NND 830              /* Next node to area                */
/*                                                                          */
#define NMA$C_PCOB_OAN 400              /* Active name                      */
#define NMA$C_PCOB_OAC 410              /* Active links                     */
#define NMA$C_PCOB_ONA 500              /* Name                             */
#define NMA$C_PCOB_OCO 510              /* Copies                           */
#define NMA$C_PCOB_OUS 511              /* User                             */
#define NMA$C_PCOB_OVE 520              /* Verification                     */
#define NMA$C_PCOB_NAM 500              /* Name                             */
#define NMA$C_PCOB_NUM 513              /* Number                           */
#define NMA$C_PCOB_FID 530              /* File id                          */
#define NMA$C_PCOB_PID 535              /* Process id                       */
#define NMA$C_PCOB_PRV 540              /* Privilege list                   */
#define NMA$C_PCOB_OCPRV 542            /* Outgoing connect privilege list  */
#define NMA$C_PCOB_USR 550              /* User id                          */
#define NMA$C_PCOB_ACC 551              /* Account                          */
#define NMA$C_PCOB_PSW 552              /* Password                         */
#define NMA$C_PCOB_PRX 560              /* Proxy access (coded byte of NMA$C_ACES_)  */
#define NMA$C_PCOB_ALO 565              /* Alias outgoing- coded byte of nma$c_alout */
#define NMA$C_PCOB_ALI 566              /* Alias incoming- coded byte of nma$c_alinc */
/*                                                                          */
#define NMA$C_PCLK_STA 0                /* State                            */
#define NMA$C_PCLK_PID 101              /* Process id                       */
#define NMA$C_PCLK_NID 102              /* Partner Node                     */
#define NMA$C_PCLK_LAD 105              /* Link address [V2 only]           */
/* entity is node rather than link !                                        */
#define NMA$C_PCLK_DLY 110              /* Round trip delay time (word)     */
#define NMA$C_PCLK_RLN 120              /* Remote link number (word)        */
#define NMA$C_PCLK_RID 121              /* Remote identification, PID or username (ascic)  */
#define NMA$C_PCLK_USR 130              /* Username of link owner (ascic)   */
#define NMA$C_PCLK_PRC 131              /* Process name of link owner (ascic)  */
/*                                                                          */
#define NMA$C_CTCIR_ZER 0               /* Seconds since last zeroed        */
#define NMA$C_CTCIR_APR 800             /* Terminating packets received     */
#define NMA$C_CTCIR_DPS 801             /* Originating packets sent         */
#define NMA$C_CTCIR_ACL 802             /* Terminating congestion loss      */
#define NMA$C_CTCIR_CRL 805             /* Corruption loss                  */
#define NMA$C_CTCIR_TPR 810             /* Transit packets received         */
#define NMA$C_CTCIR_TPS 811             /* Transit packets sent             */
#define NMA$C_CTCIR_TCL 812             /* Transit congestion loss          */
#define NMA$C_CTCIR_LDN 820             /* Circuit down                     */
#define NMA$C_CTCIR_IFL 821             /* Initialization failure           */
#define NMA$C_CTCIR_AJD 822             /* Adjacency down events            */
#define NMA$C_CTCIR_PAJ 900             /* Peak adjacencies                 */
#define NMA$C_CTCIR_BRC 1000            /* Bytes received                   */
#define NMA$C_CTCIR_BSN 1001            /* Bytes sent                       */
#define NMA$C_CTCIR_MBY 1002            /* Multicast bytes received         */
#define NMA$C_CTCIR_DBR 1010            /* Data blocks received             */
#define NMA$C_CTCIR_DBS 1011            /* Data blocks sent                 */
#define NMA$C_CTCIR_DEI 1020            /* Data errors inbound              */
#define NMA$C_CTCIR_DEO 1021            /* Data errors outbound             */
#define NMA$C_CTCIR_RRT 1030            /* Remote reply timeouts            */
#define NMA$C_CTCIR_LRT 1031            /* Local reply timeouts             */
#define NMA$C_CTCIR_RBE 1040            /* Remote buffer errors             */
#define NMA$C_CTCIR_LBE 1041            /* Local buffer errors              */
#define NMA$C_CTCIR_SIE 1050            /* Selection intervals elapsed      */
#define NMA$C_CTCIR_SLT 1051            /* Selection timeouts               */
#define NMA$C_CTCIR_UBU 1065            /* NI user buffer unavailable       */
#define NMA$C_CTCIR_RPE 1100            /* Remote process errors [V2 only]  */
#define NMA$C_CTCIR_LPE 1101            /* Local process errors [V2 only]   */
#define NMA$C_CTCIR_LIR 1240            /* Locally initiated resets         */
#define NMA$C_CTCIR_RIR 1241            /* Remotely initiated resets        */
#define NMA$C_CTCIR_NIR 1242            /* Network initiated resets         */
/*                                                                          */
#define NMA$C_CTCIR_MNE 2701            /* Multicast received for protocol  */
/* type, but not enabled                                                    */
#define NMA$C_CTCIR_ERI 2750            /* PCL Errors inbound, bit-mapped   */
/*        0  CRC error on receive                                           */
#define NMA$C_CTCIR_ERO 2751            /* PCL Errors outbound, bit-mapped  */
/*        1  CRC on transmit                                                */
#define NMA$C_CTCIR_RTO 2752            /* PCL Remote timeouts, bit-mapped  */
/*        0  Receiver busy                                                  */
#define NMA$C_CTCIR_LTO 2753            /* PCL Local timeouts               */
#define NMA$C_CTCIR_BER 2754            /* PCL Remote buffer errors         */
#define NMA$C_CTCIR_BEL 2755            /* PCL Local buffer errors          */
/*                                                                          */
#define NMA$C_CTLIN_ZER 0               /* Seconds since last zeroed        */
#define NMA$C_CTLIN_APR 800             /* Arriving packets received [V2 only]  */
#define NMA$C_CTLIN_DPS 801             /* Departing packets sent [V2 only]  */
#define NMA$C_CTLIN_ACL 802             /* Arriving congestion loss [V2 only]  */
#define NMA$C_CTLIN_TPR 810             /* Transit packets received [V2 only]  */
#define NMA$C_CTLIN_TPS 811             /* Transit packets sent [V2 only]   */
#define NMA$C_CTLIN_TCL 812             /* Transit congestion loss [V2 only]  */
#define NMA$C_CTLIN_LDN 820             /* Line down [V2 only]              */
#define NMA$C_CTLIN_IFL 821             /* Initialization failure [V2 only]  */
#define NMA$C_CTLIN_BRC 1000            /* Bytes received                   */
#define NMA$C_CTLIN_BSN 1001            /* Bytes sent                       */
#define NMA$C_CTLIN_MBY 1002            /* Multicast bytes received         */
#define NMA$C_CTLIN_DBR 1010            /* Data blocks received             */
#define NMA$C_CTLIN_DBS 1011            /* Data blocks sent                 */
#define NMA$C_CTLIN_MBL 1012            /* Multicast blocks received        */
#define NMA$C_CTLIN_BID 1013            /* Blocks sent, initially deferred  */
#define NMA$C_CTLIN_BS1 1014            /* Blocks sent, single collision    */
#define NMA$C_CTLIN_BSM 1015            /* Blocks sent, multiple collisions  */
#define NMA$C_CTLIN_DEI 1020            /* Data errors inbound              */
#define NMA$C_CTLIN_DEO 1021            /* Data errors outbound             */
#define NMA$C_CTLIN_RRT 1030            /* Remote reply timeouts            */
#define NMA$C_CTLIN_LRT 1031            /* Local reply timeouts             */
#define NMA$C_CTLIN_RBE 1040            /* Remote buffer errors             */
#define NMA$C_CTLIN_LBE 1041            /* Local buffer errors              */
#define NMA$C_CTLIN_SIE 1050            /* Selection intervals elapsed [V2 only]  */
#define NMA$C_CTLIN_SLT 1051            /* Selection timeouts [V2 only]     */
#define NMA$C_CTLIN_SFL 1060            /* Send failure                     */
#define NMA$C_CTLIN_CDC 1061            /* Collision detect check failure   */
#define NMA$C_CTLIN_RFL 1062            /* Receive failure                  */
#define NMA$C_CTLIN_UFD 1063            /* Unrecognized frame destination   */
#define NMA$C_CTLIN_OVR 1064            /* Data overrun                     */
#define NMA$C_CTLIN_SBU 1065            /* System buffer unavailable        */
#define NMA$C_CTLIN_UBU 1066            /* User buffer unavailable          */
#define NMA$C_CTLIN_RPE 1100            /* Remote process errors            */
#define NMA$C_CTLIN_LPE 1101            /* Local process errors             */
/*                                                                          */
union NMADEF1 {
    unsigned short int NMA$W_NODE;
    struct  {
        unsigned NMA$V_ADDR : 10;
        unsigned NMA$V_AREA : 6;
        } NMA$R_NODE_BITS0;
/*                                                                          */
/* Parameter ID word (DATA ID)                                              */
/*                                                                          */
    struct  {
        unsigned NMA$V_PTY_TYP : 15;    /* Type mask                        */
        unsigned NMA$V_FILL_2 : 1;
        } NMA$R_NODE_BITS1;
/*                                                                          */
/* Parameter data type byte (DATA TYPE)                                     */
/*                                                                          */
    struct  {
        unsigned NMA$V_PTY_CLE : 6;     /* Coded length mask                */
        unsigned NMA$V_PTY_MUL : 1;     /* Coded multiple indicator         */
        unsigned NMA$V_PTY_COD : 1;     /* Coded indicator                  */
        } NMA$R_NODE_BITS2;
    struct  {
        unsigned NMADEF$$_FILL_7 : 6;
        unsigned NMA$V_PTY_CMU : 2;     /* Coded multiple                   */
        } NMA$R_NODE_BITS3;
    struct  {
        unsigned NMA$V_PTY_NLE : 4;     /* Number length mask               */
        unsigned NMA$V_PTY_NTY : 2;     /* Number type mask                 */
        unsigned NMA$V_PTY_ASC : 1;     /* Ascii image indicator            */
        unsigned NMA$V_FILL_3 : 1;
        } NMA$R_NODE_BITS4;
/* NTY values (how to display number):                                      */
/* Define standard values for the DATA TYPE byte                            */
/*                                                                          */
/* Parameters for 802 control support                                       */
/*                                                                          */
/*    Circuit parameters                                                    */
/*                                                                          */
/* RSX-specific circuit parameters                                          */
/*                                                                          */
/* VMS-specific circuit NICE parameters [2700 - 2799]                       */
/*                                                                          */
/*                                                                          */
/* VMS-specific datalink only circuit parameters   [2800 - 2899]            */
/*                                                                          */
/* (these will never be used in NICE messages).                             */
/*                                                                          */
/* Server Base specific Circuit parameters                                  */
/*                                                                          */
/*    Line parameters                                                       */
/*                                                                          */
/* RSX-specific line parameters                                             */
/*                                                                          */
/*  1, Topdown                                                              */
/* VMS-specific line NICE parameters [2700 - 2799]                          */
/*                                                                          */
/* VMS-specific datalink only line parameters   [2800 - 2899]               */
/*                                                                          */
/* (these will never be used in NICE messages).                             */
/*                                                                          */
/*    Server Base specific line parameters                                  */
/*                                                                          */
/*    Communication Medium parameters                                       */
/*                                                                          */
/*    Console module parameters                                             */
/*                                                                          */
/*    Loader module parameters                                              */
/*                                                                          */
/*    Looper module parameters                                              */
/*                                                                          */
/*    Configurator module parameters                                        */
/*                                                                          */
/*    Logging parameters                                                    */
/*                                                                          */
/*    X.25 Access module parameters                                         */
/*                                                                          */
/* RSX-specific X.25-Access module parameters                               */
/*                                                                          */
/* Server Base specific X.25-Access module parameters                       */
/*                                                                          */
/*    X.25 Protocol module parameters                                       */
/*                                                                          */
/*      RSX-specific X.25-Protocol Module parameters                        */
/*                                                                          */
/* VMS-specific X25-PROTOCOL NICE parameters [2700 - 2799]                  */
/*                                                                          */
/* Server Base specific X.25-Protocol Module parameters                     */
/*                                                                          */
/*    X.25 server module parameters                                         */
/*                                                                          */
/* RSX-specific X.25-Server Module parameters                               */
/*                                                                          */
/*  1, Off                                                                  */
/*                                                                          */
/* VMS-specific X25-SERVER NICE parameters [2700 - 2799]                    */
/*                                                                          */
/* Server Base specific X.25-Server Module parameters                       */
/*                                                                          */
/*  1, Off                                                                  */
/*                                                                          */
/* X.25 trace module parameters (VMS-specific)                              */
/*                                                                          */
/*    Node parameters                                                       */
/*                                                                          */
/* RSX-Specific Node (Executor) parameters                                  */
/*                                                                          */
/*  1, Off                                                                  */
/*                                                                          */
/* VMS-specific node parameters                                             */
/*                                                                          */
/* Server Base specific Node (Executor) parameters                          */
/*                                                                          */
/*  1, Off                                                                  */
/*    Area parameters                                                       */
/*                                                                          */
/*    VMS-specific object parameters                                        */
/*                                                                          */
/*    VMS-specific link parameters                                          */
/*                                                                          */
/* CM-1/2, DU-2 (link !), HI-4 (pid)                                        */
/*    Circuit counters                                                      */
/*                                                                          */
/* VMS-specific circuit counters                                            */
/*                                                                          */
/*        2  Timeout on word                                                */
/*        1  Transmitter offline                                            */
/*        2  Receiver offline                                               */
/*    Line counters                                                         */
/*                                                                          */
/* Line counter flags (byte offset will be 0)                               */
/*                                                                          */
    } ;
#define NMA$M_CTLIN_BTL 0x8
#define NMA$M_CTLIN_FCS 0x10
#define NMA$M_CTLIN_TRJ 0x20
union NMADEF2 {
    char NMADEF$$_FILL_8;               /* byte of flags                    */
    struct  {
        unsigned NMADEF$$_FILL_9 : 3;   /* skip bits 0,1,2                  */
        unsigned NMA$V_CTLIN_BTL : 1;   /* block too long                   */
        unsigned NMA$V_CTLIN_FCS : 1;   /* frame check                      */
        unsigned NMA$V_CTLIN_TRJ : 1;   /* REJ sent                         */
        unsigned NMA$V_FILL_4 : 2;
        } NMA$R_FILL_8_BITS;
    } ;
#define NMA$M_CTLIN_RRJ 0x8
union NMADEF3 {
    char NMADEF$$_FILL_10;              /* byte of flags                    */
    struct  {
        unsigned NMADEF$$_FILL_11 : 3;  /* skip bits 0,1,2                  */
        unsigned NMA$V_CTLIN_RRJ : 1;   /* REJ received                     */
        unsigned NMA$V_FILL_5 : 4;
        } NMA$R_FILL_10_BITS;
    } ;
#define NMA$M_CTLIN_RRN 0x4
union NMADEF4 {
    char NMADEF$$_FILL_12;              /* byte of flags                    */
    struct  {
        unsigned NMADEF$$_FILL_13 : 2;  /* skip bits 0,1                    */
        unsigned NMA$V_CTLIN_RRN : 1;   /* RNR received                     */
        unsigned NMA$V_FILL_6 : 5;
        } NMA$R_FILL_12_BITS;
    } ;
#define NMA$M_CTLIN_TRN 0x4
union NMADEF5 {
    char NMADEF$$_FILL_14;              /* byte of flags                    */
    struct  {
        unsigned NMADEF$$_FILL_15 : 2;  /* skip bits 0,1                    */
        unsigned NMA$V_CTLIN_TRN : 1;   /* RNR sent                         */
        unsigned NMA$V_FILL_7 : 5;
        } NMA$R_FILL_14_BITS;
    } ;
#define NMA$M_CTLIN_INR 0x10
#define NMA$M_CTLIN_FMS 0x20
union NMADEF6 {
    char NMADEF$$_FILL_16;              /* byte of flags                    */
    struct  {
        unsigned NMADEF$$_FILL_17 : 4;  /* skip bits 0,1,2,3                */
        unsigned NMA$V_CTLIN_INR : 1;   /* invalid N(R) received            */
        unsigned NMA$V_CTLIN_FMS : 1;   /* FRMR sent                        */
        unsigned NMA$V_FILL_8 : 2;
        } NMA$R_FILL_16_BITS;
    } ;
#define NMA$M_CTLIN_TUN 0x4
#define NMA$M_CTLIN_RUN 0x10
#define NMA$M_CTLIN_FMR 0x20
#define NMA$C_CTLIN_MBS 2701            /* Multicast packets transmitted    */
#define NMA$C_CTLIN_MSN 2702            /* Multicast bytes transmitted      */
#define NMA$C_CTLIN_RME 2750            /* PCL Remote errors, bit-mapped    */
/*        0  TDM bus busy                                                   */
#define NMA$C_CTLIN_LCE 2751            /* PCL Local errors, bit-mapped     */
/*        0  Transmitter overrun                                            */
#define NMA$C_CTLIN_MSE 2752            /* PCL master/secondary errors, bit-mapped  */
/*        1  Master down                                                    */
#define NMA$C_CTNOD_ZER 0               /* Seconds since last zeroed        */
#define NMA$C_CTNOD_BRC 600             /* Bytes received                   */
#define NMA$C_CTNOD_BSN 601             /* Bytes sent                       */
#define NMA$C_CTNOD_MRC 610             /* Messages received                */
#define NMA$C_CTNOD_MSN 611             /* Messages sent                    */
#define NMA$C_CTNOD_CRC 620             /* Connects received                */
#define NMA$C_CTNOD_CSN 621             /* Connects sent                    */
#define NMA$C_CTNOD_RTO 630             /* Response timeouts                */
#define NMA$C_CTNOD_RSE 640             /* Received connect resource errors  */
#define NMA$C_CTNOD_BUN 650             /* Buffer unavailable               */
#define NMA$C_CTNOD_MLL 700             /* Maximum logical links active     */
#define NMA$C_CTNOD_APL 900             /* Aged packet loss                 */
#define NMA$C_CTNOD_NUL 901             /* Node unreachable packet loss     */
#define NMA$C_CTNOD_NOL 902             /* Node out-of-range packet loss    */
#define NMA$C_CTNOD_OPL 903             /* Oversized packet loss            */
#define NMA$C_CTNOD_PFE 910             /* Packet format error              */
#define NMA$C_CTNOD_RUL 920             /* Partial routing update loss      */
#define NMA$C_CTNOD_VER 930             /* Verification reject              */
/*                                                                          */
#define NMA$C_CTNOD_SRV_SYC 3310        /* Control buffer failures          */
#define NMA$C_CTNOD_SRV_SYS 3320        /* Small buffer failures            */
#define NMA$C_CTNOD_SRV_SYL 3330        /* Large buffer failures            */
#define NMA$C_CTNOD_SRV_SYR 3340        /* Receive buffer failures          */
/*                                                                          */
#define NMA$C_CTXP_ZER 0                /* Seconds since last zeroed        */
#define NMA$C_CTXP_BRC 1000             /* Bytes received                   */
#define NMA$C_CTXP_BSN 1001             /* Bytes sent                       */
#define NMA$C_CTXP_BLR 1010             /* Data blocks received             */
#define NMA$C_CTXP_BLS 1011             /* Data blocks sent                 */
#define NMA$C_CTXP_CRC 1200             /* Calls received                   */
#define NMA$C_CTXP_CSN 1201             /* Calls sent                       */
#define NMA$C_CTXP_FSR 1210             /* Fast selects received            */
#define NMA$C_CTXP_FSS 1211             /* Fast selects sent                */
#define NMA$C_CTXP_MSA 1220             /* Maximum switched circuits active  */
#define NMA$C_CTXP_MCA 1221             /* Maximum channels active          */
#define NMA$C_CTXP_RSE 1230             /* Received call resource errors    */
#define NMA$C_CTXP_LIR 1240             /* Locally initiated resets         */
#define NMA$C_CTXP_RIR 1241             /* Remotely initiated resets        */
#define NMA$C_CTXP_NIR 1242             /* Network initiated resets         */
#define NMA$C_CTXP_RST 1250             /* Restarts                         */
/*                                                                          */
#define NMA$C_CTXS_ZER 0                /* Seconds since last zeroed        */
#define NMA$C_CTXS_MCA 200              /* Maximum circuits active          */
#define NMA$C_CTXS_ICR 210              /* Incoming calls rejected, no resources  */
#define NMA$C_CTXS_LLR 211              /* Logical links rejected, no resources  */
/*                                                                          */
#define NMA$C_LOOP_MIX 2                /* Mixed                            */
#define NMA$C_LOOP_ONE 1                /* Ones                             */
#define NMA$C_LOOP_ZER 0                /* Zeroes                           */
/*                                                                          */
#define NMA$C_LOOP_DCNT 1               /* Default count                    */
#define NMA$C_LOOP_DSIZ 40              /* Default message size             */
/*                                                                          */
#define NMA$C_LOOP_XMIT 0               /* Transmit                         */
#define NMA$C_LOOP_RECV 1               /* Receive                          */
#define NMA$C_LOOP_FULL 2               /* Full (both transmit and receive)  */
/*                                                                          */
#define NMA$C_STATE_ON 0                /* On                               */
#define NMA$C_STATE_OFF 1               /* Off                              */
/*                                                                          */
#define NMA$C_STATE_SER 2               /* Service (circuit/line only)      */
#define NMA$C_STATE_CLE 3               /* Cleared                          */
/*                                                                          */
#define NMA$C_STATE_HOL 2               /* Hold                             */
/*                                                                          */
#define NMA$C_STATE_SHU 2               /* Shut                             */
#define NMA$C_STATE_RES 3               /* Restricted                       */
#define NMA$C_STATE_REA 4               /* Reachable                        */
#define NMA$C_STATE_UNR 5               /* Unreachable                      */
/*	PVM0001+                                                            */
#define NMA$C_PCNO_DMAD 1023            /*                                  */
/*	PVM0001-	                                                    */
#define NMA$C_ASS_ENA 0                 /* Enabled                          */
#define NMA$C_ASS_DIS 1                 /* Disabled                         */
/*                                                                          */
#define NMA$C_SUR_ENA 0                 /* Enabled                          */
#define NMA$C_SUR_DIS 1                 /* Disabled                         */
/*                                                                          */
#define NMA$C_LINSS_STA 0               /* Starting                         */
#define NMA$C_LINSS_REF 1               /* Reflecting                       */
#define NMA$C_LINSS_LOO 2               /* Looping                          */
#define NMA$C_LINSS_LOA 3               /* Loading                          */
#define NMA$C_LINSS_DUM 4               /* Dumping                          */
#define NMA$C_LINSS_TRI 5               /* Triggering                       */
#define NMA$C_LINSS_ASE 6               /* Autoservice                      */
#define NMA$C_LINSS_ALO 7               /* Autoloading                      */
#define NMA$C_LINSS_ADU 8               /* Autodumping                      */
#define NMA$C_LINSS_ATR 9               /* Autotriggering                   */
#define NMA$C_LINSS_SYN 10              /* Synchronizing                    */
#define NMA$C_LINSS_FAI 11              /* Failed                           */
#define NMA$C_LINSS_RUN 12              /* Running                          */
#define NMA$C_LINSS_UNS 13              /* Unsyncronised                    */
#define NMA$C_LINSS_IDL 14              /* Idle (PSI-only)                  */
/*                                                                          */
#define NMA$C_CIRTY_POI 0               /* DDCMP Point                      */
#define NMA$C_CIRTY_CON 1               /* DDCMP Controller                 */
#define NMA$C_CIRTY_TRI 2               /* DDCMP Tributary                  */
#define NMA$C_CIRTY_X25 3               /* X25                              */
#define NMA$C_CIRTY_DMC 4               /* DDCMP DMC compatibility mode (DMP)  */
/*/*        CIRTY_LAPB, 5                /* LAPB  *** remove once all references have been changed to LAPB *** */
#define NMA$C_CIRTY_NI 6                /* NI                               */
/*                                                                          */
#define NMA$C_LINSV_ENA 0               /* Enabled                          */
#define NMA$C_LINSV_DIS 1               /* Disabled                         */
/*                                                                          */
#define NMA$C_CIRPST_AUT 1              /* Automatic                        */
#define NMA$C_CIRPST_ACT 2              /* Active                           */
#define NMA$C_CIRPST_INA 3              /* Inactive                         */
#define NMA$C_CIRPST_DIE 4              /* Dying                            */
#define NMA$C_CIRPST_DED 5              /* Dead                             */
/*                                                                          */
#define NMA$C_CIRBLK_ENA 0              /* Enabled                          */
#define NMA$C_CIRBLK_DIS 1              /* Disabled                         */
/*                                                                          */
#define NMA$C_CIRUS_PER 0               /* Permanent                        */
#define NMA$C_CIRUS_INC 1               /* Incoming                         */
#define NMA$C_CIRUS_OUT 2               /* Outgoing                         */
/*                                                                          */
#define NMA$C_CIRHS_ENA 0               /* Enabled                          */
#define NMA$C_CIRHS_DIS 1               /* Disabled                         */
/*                                                                          */
#define NMA$C_CIRBF_UNL 255             /* Unlimited                        */
/*                                                                          */
#define NMA$C_CIRVE_ENA 0               /* Enabled                          */
#define NMA$C_CIRVE_DIS 1               /* Disabled                         */
#define NMA$C_CIRVE_INB 2               /* Inbound                          */
/*                                                                          */
#define NMA$C_CIRXPT_ZND 1              /* Z-node                           */
#define NMA$C_CIRXPT_PH2 2              /* Force Phase II on this circuit   */
#define NMA$C_CIRXPT_PH3 3              /* Routing III                      */
#define NMA$C_CIRXPT_RO3 3              /* Routing III                      */
#define NMA$C_CIRXPT_NR4 4              /* Nonrouting Phase IV              */
/*                                                                          */
#define NMA$C_DPX_FUL 0                 /* Full                             */
#define NMA$C_DPX_HAL 1                 /* Half                             */
#define NMA$C_DPX_MPT 4                 /* Multipoint                       */
/*                                                                          */
#define NMA$C_LINCN_NOR 0               /* Normal                           */
#define NMA$C_LINCN_LOO 1               /* Loop                             */
/*                                                                          */
#define NMA$C_LINPR_POI 0               /* DDCMP Point                      */
#define NMA$C_LINPR_CON 1               /* DDCMP Controller                 */
#define NMA$C_LINPR_TRI 2               /* DDCMP Tributary                  */
#define NMA$C_LINPR_DMC 4               /* DDCMP DMC compatibility mode (DMP)  */
#define NMA$C_LINPR_LAPB 5              /* LAPB                             */
#define NMA$C_LINPR_NI 6                /* NI                               */
#define NMA$C_LINPR_BSY 9               /* BISYNC (not really - just Genbyte) */
#define NMA$C_LINPR_GENBYTE 9           /* Genbyte (real name)              */
#define nma$c_linpr_lapbe 10            /* LAPBE                            */
#define nma$c_linpr_ea_hdlc 20          /* Extended addressing HDLC         */
#define nma$c_linpr_sdlc 21             /* SDLC                             */
#define nma$c_linpr_bisync 22           /* IBM Bisync protocol (not BSY framing) */
#define nma$c_linpr_swift 23            /* SWIFT Bisync variant             */
#define nma$c_linpr_chips 24            /* CHIPS Bisync variant             */
#define nma$m_linpr_mop 128             /* MOP support                      */
/*                                                                          */
#define nma$c_code_ascii 1              /* ASCII character code             */
#define nma$c_code_ebcdic 2             /* EBCDIC character code            */
/*                                                                          */
#define NMA$C_LINPR_MAS 1               /* Master (controls clock signals)  */
#define NMA$C_LINPR_NEU 2               /* Neutral (uses master's clock signals)  */
#define NMA$C_LINPR_SEC 0               /* Secondary (backup for master failure)  */
/*                                                                          */
#define NMA$C_LINCL_EXT 0               /* External                         */
#define NMA$C_LINCL_INT 1               /* Internal                         */
/*                                                                          */
#define NMA$C_LINFM_802E 0              /* 802 Extended                     */
#define NMA$C_LINFM_ETH 1               /* Ethernet                         */
#define NMA$C_LINFM_802 2               /* 802                              */
/*                                                                          */
#define NMA$C_LINSR_USR 1               /* User supplied                    */
#define NMA$C_LINSR_CLI 2               /* Class I                          */
/*                                                                          */
#define NMA$C_LINSWI_DIS 1              /* Switch disabled                  */
#define NMA$C_LINSWI_ENA 0              /* Switch enabled                   */
/*	                                                                    */
#define NMA$C_LINHNG_DIS 1              /* Hangup disabled                  */
#define NMA$C_LINHNG_ENA 0              /* Hangup enabled                   */
/*	                                                                    */
#define NMA$C_LINRES_DIS 1              /* Restart disabled                 */
#define NMA$C_LINRES_ENA 0              /* Restart enabled                  */
/*                                                                          */
#define NMA$C_LINTY_POI 0               /* DDCMP Point                      */
#define NMA$C_LINTY_CON 1               /* DDCMP Controller                 */
#define NMA$C_LINTY_TRI 2               /* DDCMP Tributary                  */
#define NMA$C_LINTY_DMC 3               /* DDCMP DMC compatibility mode (DMP)  */
/*                                                                          */
#define NMA$C_LINMC_SET 1               /* Set address(es)                  */
#define NMA$C_LINMC_CLR 2               /* Clear address(es)                */
#define NMA$C_LINMC_CAL 3               /* Clear entire list of multicast addresses  */
#define NMA$C_LINMC_SDF 4               /* Set physical address to DECnet default  */
/*                                                                          */
#define NMA$C_ACC_SHR 1                 /* Shared access (default protocol user)  */
#define NMA$C_ACC_LIM 2                 /* Limited access (point-to-point conn.)  */
#define NMA$C_ACC_EXC 3                 /* Exclusive access (allow no others)  */
/*                                                                          */
#define NMA$C_LINMO_AUT 1               /* Auto address mode                */
#define NMA$C_LINMO_SIL 2               /* Silo address mode                */
/*                                                                          */
#define NMA$C_X25MD_DTE 1               /* line operates as DTE             */
#define NMA$C_X25MD_DCE 2               /* line operates as DCE             */
#define NMA$C_X25MD_DTL 3               /* line is a DTE in loopback        */
#define NMA$C_X25MD_DCL 4               /* line is a DCE in loopback        */
#define nma$c_x25md_neg 5               /* line negotiates mode of operation */
/*                                                                          */
#define nma$c_x25red_busy 0             /* redirected beacuse DTE was Busy  */
#define nma$c_x25red_out_of_order 1     /* redirected beacuse DTE was out of order */
#define nma$c_x25red_systematic 2       /* redirected systematically        */
/*                                                                          */
#define NMA$C_NODTY_ROU 0               /* Routing Phase III                */
#define NMA$C_NODTY_NON 1               /* Nonrouting Phase III             */
#define NMA$C_NODTY_PHA 2               /* Phase II                         */
#define NMA$C_NODTY_AREA 3              /* Area                             */
#define NMA$C_NODTY_RT4 4               /* Routing Phase IV                 */
#define NMA$C_NODTY_NR4 5               /* Nonrouting Phase IV              */
/*                                                                          */
#define NMA$C_NODINB_ROUT 1             /* Router                           */
#define NMA$C_NODINB_ENDN 2             /* Endnode                          */
/*                                                                          */
#define NMA$C_NODPW_SET 0               /* Password set                     */
/*                                                                          */
#define NMA$C_CPU_8 0                   /* PDP-8 processor                  */
#define NMA$C_CPU_11 1                  /* PDP-11 processor                 */
#define NMA$C_CPU_1020 2                /* Decsystem 10/20 processor        */
#define NMA$C_CPU_VAX 3                 /* Vax processor                    */
/*                                                                          */
#define NMA$C_NODSNV_PH3 0              /* Phase III                        */
#define NMA$C_NODSNV_PH4 1              /* Phase IV                         */
/*                                                                          */
#define NMA$C_SOFT_SECL 0               /* Secondary loader                 */
#define NMA$C_SOFT_TERL 1               /* Tertiary loader                  */
#define NMA$C_SOFT_OSYS 2               /* Operating system                 */
#define NMA$C_SOFT_DIAG 3               /* Diagnostics                      */
/*                                                                          */
#define NMA$C_ACES_NONE 0               /* None                             */
#define NMA$C_ACES_INCO 1               /* Incoming                         */
#define NMA$C_ACES_OUTG 2               /* Outgoing                         */
#define NMA$C_ACES_BOTH 3               /* Both                             */
#define NMA$C_ACES_REQU 4               /* Required                         */
/*                                                                          */
#define NMA$C_ALIINC_ENA 0              /* Enabled                          */
#define NMA$C_ALIINC_DIS 1              /* Disabled                         */
/*                                                                          */
#define NMA$C_ALOUT_ENA 0               /* Enabled                          */
#define NMA$C_ALOUT_DIS 1               /* Disabled                         */
/*                                                                          */
#define NMA$C_ALINC_ENA 0               /* Enabled                          */
#define NMA$C_ALINC_DIS 1               /* Disabled                         */
/*                                                                          */
#define NMA$C_PRXY_ENA 0                /* Enabled                          */
#define NMA$C_PRXY_DIS 1                /* Disabled                         */
/*                                                                          */
#define NMA$C_PSPCY_NOR 0               /* Normal                           */
#define NMA$C_PSPCY_INT 1               /* Interim                          */
/*                                                                          */
#define NMA$C_XPRTY_BIL 1               /* Bilateral                        */
/*                                                                          */
#define NMA$C_XPRST_ON 0                /* On                               */
#define NMA$C_XPRST_OFF 1               /* Off                              */
#define NMA$C_XPRST_SHU 2               /* Shut                             */
/*                                                                          */
#define NMA$C_XPRMN_ENA 0               /* Enabled                          */
#define NMA$C_XPRMN_DIS 1               /* Disabled                         */
/*                                                                          */
#define NMA$C_XPRSB_RUN 12              /* Running                          */
#define NMA$C_XPRSB_UNS 13              /* Unsynchronized                   */
#define NMA$C_XPRSB_SYN 10              /* Synchronizing                    */
/*                                                                          */
#define NMA$C_Clear_String 0            /*Clear string value                */
#define NMA$C_Clear_Longword -1         /*Clear longword value              */
#define NMA$C_CAL_CLR 0                 /*Call clear                        */
#define NMA$C_CAL_NOW 1                 /*Call now                          */
#define NMA$C_DAY_ALL 0
#define NMA$C_DAY_MON 1
#define NMA$C_DAY_TUE 2
#define NMA$C_DAY_WED 3
#define NMA$C_DAY_THU 4
#define NMA$C_DAY_FRI 5
#define NMA$C_DAY_SAT 6
#define NMA$C_DAY_SUN 7
#define NMA$C_TIC_No_Cut 0              /*Inhibit timecutting               */
#define NMA$C_TIC_Cut 1                 /*Perform Timecutting               */
#define NMA$C_CSG_No_Signal 0           /*Inhibit call-signal data          */
#define NMA$c_CSG_Signal 1              /*Send call-signal data             */
#define NMA$c_IRC_DIS 0                 /*Incoming Reverse Disable          */
#define NMA$c_IRC_ENA 1                 /*Incoming Reverse Enable           */
#define NMA$c_ORC_DIS 0                 /*Outgoing Reverse Enable           */
#define NMA$c_ORC_ENA 1                 /*Outgoing Reverse Disable          */
#define NMA$c_RED_DIS 0                 /*Redirect Enable                   */
#define NMA$c_RED_ENA 1                 /*Redirect Disable                  */
#define NMA$c_MOD_NOAUTO 0              /*Mode AUTO time-cutting            */
#define NMA$c_MOD_AUTO 1                /*Mode non-auto time-cutting        */
#define NMA$c_SWC_DIS 0                 /*Enable switched mode              */
#define NMA$c_SWC_ENA 1                 /*Set line for Leased operation     */
#define NMA$c_MDM_OFF 0                 /*Enable modem signals              */
#define NMA$c_MDM_ON 1                  /*Disable modem signals             */
#define NMA$c_DTS_NO_CABLE 1            /*DTE does not have X21 cable       */
#define NMA$c_DTS_NO_X21_CABLE 2        /*DTE has none-X21 cable.           */
#define NMA$c_DTS_READY 3               /*DCE is not ready                  */
#define NMA$c_DTS_NOT_READY 4           /*DTE is signalling Not-Ready to network. */
#define NMA$c_DTS_ACTIVE 5              /*DTE in normal working mode.       */
#define NMA$c_DTS_NO_OUTGOING 6         /*Outgoing calls prohibitedin normal working mode. */
#define NMA$c_CAS_NONE 1                /*Call-Status - No call active      */
#define NMA$c_CAS_OUT 2                 /*Outgoing call active              */
#define NMA$c_CAS_IN 3                  /*Incoming call active              */
#define NMA$c_CAS_OUT_R 4               /*Outgoing reverse active           */
#define NMA$c_CAS_IN_R 5                /*Incoming reverse active           */
#define NMA$c_DTL_ACCEPT 1              /*Accept call from                  */
#define NMA$c_DTL_REJECT 2              /*Reject call from                  */
#define NMA$C_CAC_MAN 1                 /*X21 controls connect/accept       */
#define NMA$C_CAC_AUTO_CONNECT 2        /*Driver connects automatically     */
#define NMA$C_CAC_AUTO_ACCEPT 3         /*Enhanced subaddressing            */
/*                                                                          */
#define NMA$C_JAN 1
#define NMA$C_FEB 2
#define NMA$C_MAR 3
#define NMA$C_APR 4
#define NMA$C_MAY 5
#define NMA$C_JUN 6
#define NMA$C_JUL 7
#define NMA$C_AUG 8
#define NMA$C_SEP 9
#define NMA$C_OCT 10
#define NMA$C_NOV 11
#define NMA$C_DEC 12
/*                                                                          */
#define NMA$C_SOFD_DP 0                 /* DP11-DA (OBSOLETE)               */
#define NMA$C_SOFD_UNA 1                /* DEUNA UNIBUS CSMA/CD communication link */
#define NMA$C_SOFD_DU 2                 /* DU11-DA synchronous line interface */
#define NMA$C_SOFD_CNA 3                /* DECNA CSMA/CD communication link */
#define NMA$C_SOFD_DL 4                 /* DL11-C, -E, or -WA synchronous line interface */
#define NMA$C_SOFD_QNA 5                /* DEQNA CSMA/CD communication link */
#define NMA$C_SOFD_DQ 6                 /* DQ11-DA (OBSOLETE)               */
#define NMA$C_SOFD_CI 7                 /* Computer Interconnect Interface  */
#define NMA$C_SOFD_DA 8                 /* DA11-B or -AL UNIBUS link        */
#define NMA$C_SOFD_PCL 9                /* PCL11-B multiple CPU link        */
#define NMA$C_SOFD_DUP 10               /* DUP11-DA synchronous line interface */
#define NMA$C_SOFD_LUA 11               /* DELUA CSMA/CD communication link */
#define NMA$C_SOFD_DMC 12               /* DMC11-DA/AR, -FA/AR, -MA/AL or -MD/AL interprocessor link */
#define NMA$C_SOFD_LNA 13               /* MicroServer Lance CSMA/CD communication link */
#define NMA$C_SOFD_DN 14                /* DN11-BA or -AA automatic calling unit */
#define NMA$C_SOFD_DLV 16               /* DLV11-E, -F, -J, MXV11-A or -B asynchronous line */
#define NMA$C_SOFD_LCS 17               /* Lance/Decserver 100 CSMA/CD communication link */
#define NMA$C_SOFD_DMP 18               /* DMP11 multipoint interprocessor link */
#define NMA$C_SOFD_AMB 19               /* AMBER (OBSOLETE)                 */
#define NMA$C_SOFD_DTE 20               /* DTE20 PDP-11 to KL10 interface   */
#define NMA$C_SOFD_DBT 21               /* DEBET CSMA/CD communication link */
#define NMA$C_SOFD_DV 22                /* DV11-AA/BA synchronous line multiplexer */
#define NMA$C_SOFD_BNA 23               /* DEBNA BI CSMA/CD communication link */
#define NMA$C_SOFD_BNT 23               /* DEBNT **obsolete**               */
#define NMA$C_SOFD_DZ 24                /* DZ11-A, -B, -C, -D asynchronous line multiplexer */
#define NMA$C_SOFD_LPC 25               /* LANCE/PCXX CSMA/CD communication link */
#define NMA$C_SOFD_DSV 26               /* DSV11 Q-bus synchronous link     */
#define NMA$C_SOFD_CEC 27               /* 3-COM/IBM-PC CSMA/CD communication link */
#define NMA$C_SOFD_KDP 28               /* KMC11/DUP11-DA synchronous line multiplexer */
#define NMA$C_SOFD_IEC 29               /* Interlan/IBM-PC CSMA/CD communication link */
#define NMA$C_SOFD_KDZ 30               /* KMC11/DZ11-A, -B, -C, or -D asynchronous line multiplexer */
#define NMA$C_SOFD_UEC 31               /* Univation/RAINBOW-100 CSMA/CD communication link */
#define NMA$C_SOFD_KL8 32               /* KL8-J (OBSOLETE)                 */
#define NMA$C_SOFD_DS2 33               /* LANCE/DECserver 200 CSMA/CD communication link */
#define NMA$C_SOFD_DMV 34               /* DMV11 interprocessor link        */
#define NMA$C_SOFD_DS5 35               /* DECserver 500 CSMA/CD communication link */
#define NMA$C_SOFD_DPV 36               /* DPV11 synchronous line interface */
#define NMA$C_SOFD_LQA 37               /* DELQA CSMA/CD communication link */
#define NMA$C_SOFD_DMF 38               /* DMF32 synchronous line unit      */
#define NMA$C_SOFD_SVA 39               /* DESVA CSMA/CD communication link */
#define NMA$C_SOFD_DMR 40               /* DMR11-AA, -AB, -AC, or -AE interprocessor link */
#define NMA$C_SOFD_MUX 41               /* MUXserver 100 CSMA/CD communication link */
#define NMA$C_SOFD_KMY 42               /* KMS11-PX synchronous line interface with X.25 Level 2 microcode */
#define NMA$C_SOFD_DEP 43               /* DEPCA PCSG/IBM-PC CSMA/CD communication link */
#define NMA$C_SOFD_KMX 44               /* KMS11-BD/BE synchronous line interface with X.25 Level 2 microcode */
#define NMA$C_SOFD_LTM 45               /* LTM (911) Ethernet monitor       */
#define NMA$C_SOFD_DMB 46               /* DMB-32 BI synchronous line multiplexer */
#define NMA$C_SOFD_DES 47               /* DESNC Ethernet Encryption Module */
#define NMA$C_SOFD_KCP 48               /* KCP synchronous/asynchronous line */
#define NMA$C_SOFD_MX3 49               /* MUXServer 300 CSMA/CD communication link  */
#define NMA$C_SOFD_SYN 50               /* MicroServer synchronous line interface */
#define NMA$C_SOFD_MEB 51               /* DEMEB multiport bridge CSMA/CD communication link  */
#define NMA$C_SOFD_DSB 52               /* DSB32 BI synchronous line interface */
#define NMA$C_SOFD_BAM 53               /* DEBAM LANBridge-200 Data Link    */
#define NMA$C_SOFD_DST 54               /* DST-32 TEAMmate synchronous line interface (DEC423) */
#define NMA$C_SOFD_FAT 55               /* DEFAT DataKit Server CSMA/CD communication link  */
#define NMA$C_SOFD_RSM 56               /* DERSM - Remote Segment Monitor   */
#define NMA$C_SOFD_RES 57               /* DERES - Remote Environmental Sensor  */
#define NMA$C_SOFD_3C2 58               /* 3COM Etherlink II (part number 3C503)  */
#define NMA$C_SOFD_3CM 59               /* 3COM Etherlink/MC (part number 3C523)  */
#define NMA$C_SOFD_DS3 60               /* DECServer 300 CSMA/CD communication link  */
#define NMA$C_SOFD_MF2 61               /* Mayfair-2 CSMA/CD communication link  */
#define NMA$C_SOFD_MMR 62               /* DEMMR Ethernet Multiport Manageable Repeater  */
#define NMA$C_SOFD_VIT 63               /* Vitalink TransLAN III/IV (NP3A) Bridge  */
#define NMA$C_SOFD_VT5 64               /* Vitalink TransLAN 350 (NPC25) Bridge  */
#define NMA$C_SOFD_BNI 65               /* DEBNI BI CSMA/CD communication link  */
#define NMA$C_SOFD_MNA 66               /* DEMNA XMI CSMA/CD communication link  */
#define NMA$C_SOFD_PMX 67               /* PMAX (KN01) CSMA/CD communication link  */
#define NMA$C_SOFD_NI5 68               /* Interlan NI5210-8 CSMA/CD comm link for IBM PC XT/AT  */
#define NMA$C_SOFD_NI9 69               /* Interlan NI9210 CSMA/CD comm link for IBM PS/2  */
#define NMA$C_SOFD_KMK 70               /* KMS11-K DataKit UNIBUS adapter   */
#define NMA$C_SOFD_3CP 71               /* Etherlink Plus (part number 3C505)  */
#define NMA$C_SOFD_DP2 72               /* DPNserver-200 CSMA/CD communication link  */
#define NMA$C_SOFD_ISA 73               /* SGEC CSMA/CD communication link  */
#define NMA$C_SOFD_DIV 74               /* DIV-32 DEC WAN controller-100    */
#define NMA$C_SOFD_QTA 75               /* DEQTA CSMA/CD communication link */
#define NMA$C_SOFD_DSF 101              /* DSF32 2 line sync comm link for Cirrus */
#define NMA$C_SOFD_KFE 104              /* KFE52 CSMA/CD comm link for Cirrus */
/*                                                                          */
#define NMA$_SUCCESS 1                  /* Unqualified success              */
#define NMA$_SUCCFLDRPL 9               /* Success with field replaced      */
#define NMA$_BADFID 0                   /* Invalid field id code            */
#define NMA$_BADDAT 8                   /* Invalid data format              */
#define NMA$_BADOPR 16                  /* Invalid operation                */
#define NMA$_BUFTOOSMALL 24             /* Buffer too small                 */
#define NMA$_FLDNOTFND 32               /* Field not found                  */
/*                                                                          */
#define NMA$C_OPN_MIN 0                 /* Minimum !                        */
#define NMA$C_OPN_NODE 0                /* Nodes                            */
#define NMA$C_OPN_LINE 1                /* Lines                            */
#define NMA$C_OPN_LOG 2                 /* Logging                          */
#define NMA$C_OPN_OBJ 3                 /* Object                           */
#define NMA$C_OPN_CIR 4                 /* Circuit                          */
#define NMA$C_OPN_X25 5                 /* Module X25                       */
#define NMA$C_OPN_X29 6                 /* Module X29                       */
#define NMA$C_OPN_CNF 7                 /* Module Configurator              */
#define NMA$C_OPN_MAX 7                 /* Maximum ! permanent database files  */
#define NMA$C_OPN_ALL 127               /* All opened files                 */
/*                                                                          */
#define NMA$C_OPN_AC_RO 0               /* Read Only                        */
#define NMA$C_OPN_AC_RW 1               /* Read write                       */
/*                                                                          */
#define NMA$C_FN2_DLL 2                 /* Down line load                   */
#define NMA$C_FN2_ULD 3                 /* Upline Dump                      */
#define NMA$C_FN2_TRI 4                 /* Trigger remote bootstrap         */
#define NMA$C_FN2_LOO 5                 /* Loop back test                   */
#define NMA$C_FN2_TES 6                 /* Send test message to be looped   */
#define NMA$C_FN2_SET 7                 /* Set parameter                    */
#define NMA$C_FN2_REA 8                 /* Read Parameter                   */
#define NMA$C_FN2_ZER 9                 /* Zero counters                    */
#define NMA$C_FN2_LNS 14                /* Line service                     */
/*                                                                          */
#define NMA$C_OP2_CHNST 5               /* Node operational status          */
#define NMA$C_OP2_CHLST 8               /* Line operational status          */
/*                                                                          */
#define NMA$C_OP2_RENCT 0               /* Local node counters              */
#define NMA$C_OP2_RENST 1               /* local node status                */
#define NMA$C_OP2_RELCT 4               /* Line counters                    */
#define NMA$C_OP2_RELST 5               /* Line status                      */
/*                                                                          */
#define NMA$C_OP2_ZENCT 0               /* Local Node counters              */
#define NMA$C_OP2_ZELCT 2               /* Line counters                    */
/*                                                                          */
#define NMA$C_EN2_KNO 0                 /* Known lines                      */
#define NMA$C_EN2_LID 1                 /* Line id                          */
#define NMA$C_EN2_LCN 2                 /* Line convenience name            */
/*                                                                          */
#define NMA$C_STS_SUC 1                 /* Success                          */
#define NMA$C_STS_MOR 2                 /* Request accepted, more to come   */
#define NMA$C_STS_PAR 3                 /* Partial reply                    */
/*                                                                          */
#define NMA$C_STS_DON -128              /* Done                             */
/*                                                                          */
#define NMA$C_STS_FUN -1                /* Unrecognized function or option  */
#define NMA$C_STS_INV -2                /* Invalid message format           */
#define NMA$C_STS_PRI -3                /* Privilege violation              */
#define NMA$C_STS_SIZ -4                /* Oversized management command message  */
#define NMA$C_STS_MPR -5                /* Network management program error  */
#define NMA$C_STS_PTY -6                /* Unrecognized parameter type      */
#define NMA$C_STS_MVE -7                /* Incompatible management version  */
#define NMA$C_STS_CMP -8                /* Unrecognised component           */
#define NMA$C_STS_IDE -9                /* Invalid identification format    */
#define NMA$C_STS_LCO -10               /* Line communication error         */
#define NMA$C_STS_STA -11               /* Component in wrong state         */
#define NMA$C_STS_FOP -13               /* File open error                  */
#define NMA$C_STS_FCO -14               /* Invalid file contents            */
#define NMA$C_STS_RES -15               /* Resource error                   */
#define NMA$C_STS_PVA -16               /* Invalid parameter value          */
#define NMA$C_STS_LPR -17               /* Line protocol error              */
#define NMA$C_STS_FIO -18               /* File i/o error                   */
#define NMA$C_STS_MLD -19               /* Mirror link disconnected         */
#define NMA$C_STS_ROO -20               /* No room for new entry            */
#define NMA$C_STS_MCF -21               /* Mirror connect failed            */
#define NMA$C_STS_PNA -22               /* Parameter not applicable         */
#define NMA$C_STS_PLO -23               /* Parameter value too long         */
#define NMA$C_STS_HAR -24               /* Hardware failure                 */
#define NMA$C_STS_OPE -25               /* Operation failure                */
#define NMA$C_STS_SYS -26               /* System-specific management       */
/* function not supported                                                   */
#define NMA$C_STS_PGP -27               /* Invalid parameter grouping       */
#define NMA$C_STS_BLR -28               /* Bad loopback response            */
#define NMA$C_STS_PMS -29               /* Parameter missing                */
/*                                                                          */
#define NMA$C_STS_ALI -127              /* Invalid alias identification     */
#define NMA$C_STS_OBJ -126              /* Invalid object identification    */
#define NMA$C_STS_PRO -125              /* Invalid process identification   */
#define NMA$C_STS_LNK -124              /* Invalid link identification      */
/*                                                                          */
#define NMA$C_FOPDTL_PDB 0              /* Permanent database               */
#define NMA$C_FOPDTL_LFL 1              /* Load file                        */
#define NMA$C_FOPDTL_DFL 2              /* Dump file                        */
#define NMA$C_FOPDTL_SLF 3              /* Secondary loader                 */
#define NMA$C_FOPDTL_TLF 4              /* Tertiary loader                  */
#define NMA$C_FOPDTL_SDF 5              /* Secondary dumper                 */
#define NMA$C_FOPDTL_PDR 6              /* Permanent Database,on remote node */
#define NMA$C_FOPDTL_MFL 7              /* Management file                  */
/*                                                                          */
#define NMA$C_NCEDTL_NNA 0              /* No node name set                 */
#define NMA$C_NCEDTL_INN 1              /* Invalid node name format         */
#define NMA$C_NCEDTL_UNA 2              /* Unrecognised node name           */
#define NMA$C_NCEDTL_UNR 3              /* Node unreachable                 */
#define NMA$C_NCEDTL_RSC 4              /* Network resources                */
#define NMA$C_NCEDTL_RJC 5              /* Rejected by object               */
#define NMA$C_NCEDTL_ONA 6              /* Invalid object name format       */
#define NMA$C_NCEDTL_OBJ 7              /* Unrecognised object              */
#define NMA$C_NCEDTL_ACC 8              /* Access control rejected          */
#define NMA$C_NCEDTL_BSY 9              /* Object too busy                  */
#define NMA$C_NCEDTL_NRS 10             /* No response from object          */
#define NMA$C_NCEDTL_NSD 11             /* Node shut down                   */
#define NMA$C_NCEDTL_DIE 12             /* Node or object failed            */
#define NMA$C_NCEDTL_DIS 13             /* Disconnect by object             */
#define NMA$C_NCEDTL_ABO 14             /* Abort by object                  */
#define NMA$C_NCEDTL_ABM 15             /* Abort by management              */
/*                                                                          */
#define NMA$C_OPEDTL_DCH 0              /*	Data check                  */
#define NMA$C_OPEDTL_TIM 1              /*	Timeout                     */
#define NMA$C_OPEDTL_ORN 2              /*	Data overrun                */
#define NMA$C_OPEDTL_ACT 3              /*	Unit is active              */
#define NMA$C_OPEDTL_BAF 4              /*	Buffer allocation failure   */
#define NMA$C_OPEDTL_RUN 5              /*	Protocol running            */
#define NMA$C_OPEDTL_DSC 6              /*	Line disconnected           */
#define NMA$C_OPEDTL_FTL 8              /*	Fatal hardware error        */
#define NMA$C_OPEDTL_MNT 11             /*	DDCMP maintainance message received */
#define NMA$C_OPEDTL_LST 12             /*	Data lost due to buffer size mismatch */
#define NMA$C_OPEDTL_THR 13             /*	Threshold error             */
#define NMA$C_OPEDTL_TRB 14             /*	Tributary malfunction       */
#define NMA$C_OPEDTL_STA 15             /*	DDCMP start message received */
union NMADEF7 {
    char NMADEF$$_FILL_18;              /* byte of flags                    */
    struct  {
        unsigned NMADEF$$_FILL_19 : 2;  /* skip bits 0,1                    */
        unsigned NMA$V_CTLIN_TUN : 1;   /* transmit underrun                */
        unsigned NMADEF$$_FILL_20 : 1;  /* skip bit 3                       */
        unsigned NMA$V_CTLIN_RUN : 1;   /* receive underrun                 */
        unsigned NMA$V_CTLIN_FMR : 1;   /* FRMR received                    */
        unsigned NMA$V_FILL_9 : 2;
        } NMA$R_FILL_18_BITS;
/*                                                                          */
/* VMS-specific line counters                                               */
/*                                                                          */
/*        1  Message rejected                                               */
/*        2  Message truncated                                              */
/*        3  Receiver offline                                               */
/*        4  Receiver busy                                                  */
/*        5  Transmitter offline                                            */
/*        1  CRC error on transmit                                          */
/*        2  CRC error on receive                                           */
/*        3  Timeouts                                                       */
/*        4  Non-existant memory transmit                                   */
/*        5  Non-existant memory receive                                    */
/*        6  Buffer to small                                                */
/*        7  Failed to open channel                                         */
/*        8  Memory overflow                                                */
/*        2  Now master                                                     */
/*                                                                          */
/*    Node counters                                                         */
/*                                                                          */
/* Server Base Specific Executor Node Counters                              */
/*                                                                          */
/*        X.25 Protocol module counters                                     */
/*                                                                          */
/*        X.25 Server module counters                                       */
/*                                                                          */
/*        Coded parameter values                                            */
/*                                                                          */
/*                                                                          */
/* Loop test block type coded values                                        */
/*                                                                          */
/* Default values for loop functions                                        */
/*                                                                          */
/* Values for LOOP HELP                                                     */
/*                                                                          */
/* State coded values                                                       */
/*                                                                          */
/*    circuit/line/process specific state values                            */
/*                                                                          */
/*    logging specific state values                                         */
/*                                                                          */
/*    node specific state values                                            */
/*                                                                          */
/*	                                                                    */
/*	Default value for EXECUTOR MAXIMUM ADDRESS.                         */
/*	Note: DNA Network Management does not specify a default.            */
/*	      This is defined for VMS only, for compatibility with          */
/*	      previous releases that used a hard coded value in             */
/*	      [NETACP.SRC]NETCONFIG.MAR.                                    */
/*                                                                          */
/*                                                                          */
/*                                                                          */
/* Looper/loader assistance coded values                                    */
/*                                                                          */
/* Configurator surveillance coded values                                   */
/*                                                                          */
/* Circuit/Line substate coded values                                       */
/*                                                                          */
/* Circuit type coded values   [In V2, line type coded values]              */
/*                                                                          */
/*        Circuit/Line Service                                              */
/*                                                                          */
/* Circuit polling state                                                    */
/*                                                                          */
/* Circuit blocking values                                                  */
/*                                                                          */
/* Circuit usage values                                                     */
/*                                                                          */
/* Circuit parameter, Handshake Required                                    */
/*                                                                          */
/* Circuit maximum receive buffers                                          */
/*                                                                          */
/* Circuit verification    [VMS only]                                       */
/*                                                                          */
/* Circuit (desired) transport type    [VMS only]                           */
/*                                                                          */
/* Line duplex coded values                                                 */
/*                                                                          */
/* Line controller mode                                                     */
/*                                                                          */
/* Line protocol values (same as CIRTY_)                                    */
/*                                                                          */
/*	Character encoding                                                  */
/*                                                                          */
/* Line protocol values for the PCL-11B                                     */
/*                                                                          */
/* Line clock values                                                        */
/*                                                                          */
/* Line packet format types                                                 */
/*                                                                          */
/* Line services                                                            */
/*                                                                          */
/* Line Switch states                                                       */
/*                                                                          */
/* Line Hangup state                                                        */
/*                                                                          */
/* Line Restart state                                                       */
/*                                                                          */
/* Line type coded values  [V2 only]                                        */
/*                                                                          */
/* Line multicast address function code [VMS datalink only].                */
/* Destination and physical address function codes too [VMS datalink only]. */
/*                                                                          */
/* NI line protocol access mode  [VMS datalink only]                        */
/*                                                                          */
/* PCL-11B address mode                                                     */
/*                                                                          */
/* X.25 line mode                                                           */
/*                                                                          */
/* X.25 server redirect reason                                              */
/*                                                                          */
/* Node type values                                                         */
/*                                                                          */
/* Node inbound states                                                      */
/*                                                                          */
/* Node password values                                                     */
/*                                                                          */
/* Node CPU type codes                                                      */
/*                                                                          */
/* Service node version coded values                                        */
/*                                                                          */
/* Node software type code                                                  */
/*                                                                          */
/* Node access (and default access) codes                                   */
/*                                                                          */
/* Executor Alias incoming values                                           */
/*                                                                          */
/* Object alias outgoing                                                    */
/*                                                                          */
/* Object alias incoming                                                    */
/*                                                                          */
/* Executor Proxy                                                           */
/*                                                                          */
/* Path Split Policy                                                        */
/*                                                                          */
/* X.25 Protocol type values                                                */
/*                                                                          */
/* X.25 protocol state values                                               */
/*                                                                          */
/* X.25 protocol multi-network support flag                                 */
/*                                                                          */
/* X.25 protocol DTE substate values                                        */
/*                                                                          */
/* X21 literals                                                             */
/*	                                                                    */
/*  Months of the Year Codes                                                */
/*                                                                          */
/* Service device codes (MOP)                                               */
/*                                                                          */
/*        Status codes for field support routines                           */
/*                                                                          */
/*        Permanent database file ID codes                                  */
/*                                                                          */
/*        Open access codes                                                 */
/*                                                                          */
/*        Define Phase II NICE function codes                               */
/*                                                                          */
/*        Change parameters (volatile only)                                 */
/*                                                                          */
/*        Read Information (Status and Counters only)                       */
/*                                                                          */
/*        Zero counters                                                     */
/*                                                                          */
/*        Line entity codes                                                 */
/*                                                                          */
/* NML Return codes                                                         */
/*                                                                          */
/*        Error details                                                     */
/*                                                                          */
/*                                                                          */
/*        STS_FOP and STS_FIO                                               */
/*                                                                          */
/*        STS_MLD, STS_MCF                                                  */
/*                                                                          */
/*        STS_OPE                                                           */
/*                                                                          */
    } ;
 
#pragma __member_alignment __restore
