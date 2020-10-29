/* kl10_fe.c: KL-10 front end (console terminal) simulator

   Copyright (c) 2019-2020, Richard Cornwell

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

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#include "kx10_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#if KL
#define UNIT_DUMMY      (1 << UNIT_V_UF)

#define DTE_DEVNUM       0200
#define DEV_V_OS        (DEV_V_UF + 1)                 /* Type RSX10/RSX20 */
#define DEV_M_OS        (1 << DEV_V_OS)
#define TYPE_RSX10      (0 << DEV_V_OS)
#define TYPE_RSX20      (1 << DEV_V_OS)



/* DTE10 CONI bits */

#define DTE_RM         00100000    /* Restricted mode */
#define DTE_D11        00040000    /* Dead-11 */
#define DTE_11DB       00020000    /* TO11 Door bell request */
#define DTE_10DB       00001000    /* TO10 Door bell request */
#define DTE_11ER       00000400    /* Error during TO11 transfer */
#define DTE_11DN       00000100    /* TO 11 transfer done */
#define DTE_10DN       00000040    /* TO 10 transfer done */
#define DTE_10ER       00000020    /* Error during TO10 transfer */
#define DTE_PIE        00000010    /* PIO enabled */
#define DTE_PIA        00000007    /* PI channel assigment */

/* internal flags */
#define DTE_11RELD     01000000    /* Reload 11. */
#define DTE_TO11       02000000    /* Transfer to 11 */
#define DTE_SEC        04000000    /* In secondary protocol */
#define DTE_IND        010000000   /* Next transfer will be indirect */
#define DTE_SIND       020000000   /* Send indirect data next */

/* DTE CONO bits */
#define DTE_CO11DB     0020000     /* Set TO11 Door bell */
#define DTE_CO11CR     0010000     /* Clear reload 11 button */
#define DTE_CO11SR     0004000     /* Set reload 11 button */
#define DTE_CO10DB     0001000     /* Clear TO10 Door bell */
#define DTE_CO11CL     0000100     /* Clear TO11 done and error */
#define DTE_CO10CL     0000040     /* Clear TO10 done and error */
#define DTE_PIENB      0000020     /* Load PI and enable bit */

/* DTE DATAO */
#define DTE_TO10IB     010000      /* Interrupt after transfer */
#define DTE_TO10BC     007777      /* Byte count for transfer */

/* Secondary protocol addresses */
#define SEC_DTFLG      0444        /* Operation complete flag */
#define SEC_DTCLK      0445        /* Clock interrupt flag */
#define SEC_DTCI       0446        /* Clock interrupt instruction */
#define SEC_DTT11      0447        /* 10 to 11 argument */
#define SEC_DTF11      0450        /* 10 from 11 argument */
#define SEC_DTCMD      0451        /* To 11 command word */
#define SEC_DTSEQ      0452        /* Operation sequence number */
#define SEC_DTOPR      0453        /* Operational DTE # */
#define SEC_DTCHR      0454        /* Last typed character */
#define SEC_DTMTD      0455        /* Monitor tty output complete flag */
#define SEC_DTMTI      0456        /* Monitor tty input flag */
#define SEC_DTSWR      0457        /* 10 switch register */

#define SEC_PGMCTL     00400
#define SEC_ENDPASS    00404
#define SEC_LOOKUP     00406
#define SEC_RDWRD      00407
#define SEC_RDBYT      00414
#define SEC_ESEC       00440
#define SEC_EPRI       00500
#define SEC_ERTM       00540
#define SEC_CLKCTL     01000
#define SEC_CLKOFF     01000
#define SEC_CLKON      01001
#define SEC_CLKWT      01002
#define SEC_CLKRD      01003
#define SEC_RDSW       01400
#define SEC_CLRDDT     03000
#define SEC_SETDDT     03400
#define SEC_MONO       04000
#define SEC_MONON      04400
#define SEC_SETPRI     05000
#define SEC_RTM        05400
#define SEC_CMDMSK     07400
#define DTE_MON        00000001     /* Save in unit1 STATUS */
#define SEC_CLK        00000002     /* Clock enabled */
#define ITS_ON         00000004     /* ITS Is alive */

/* Primary or Queued protocol addresses */
#define PRI_CMTW_0     0
#define PRI_CMTW_PPT   1            /* Pointer to com region */
#define PRI_CMTW_STS   2            /* Status word */
#define PRI_CMT_PWF    SMASK        /* Power failure */
#define PRI_CMT_L11    BIT1         /* Load 11 */
#define PRI_CMT_INI    BIT2         /* Init */
#define PRI_CMT_TST    BIT3         /* Valid examine bit */
#define PRI_CMT_QP     020000000LL  /* Do Queued protocol */
#define PRI_CMT_FWD    001000000LL  /* Do full word transfers */
#define PRI_CMT_IP     RSIGN        /* Indirect transfer */
#define PRI_CMT_TOT    0200000LL    /* TOIT bit */
#define PRI_CMT_10IC   0177400LL    /* TO10 IC for queued transfers */
#define PRI_CMT_11IC   0000377LL    /* TO11 IC for queued transfers */
#define PRI_CMTW_CNT   3            /* Queue Count */
#define PRI_CMTW_KAC   5            /* Keep alive count */
#define PRI_IND_FLG    0100000      /* Flag function as indirect */

#define PRI_EM2EI      001          /* Initial message to 11 */
#define PRI_EM2TI      002          /* Replay to initial message. */
#define PRI_EMSTR      003          /* String data */
#define PRI_EMLNC      004          /* Line-Char */
#define PRI_EMRDS      005          /* Request device status */
#define PRI_EMOPS      006
#define PRI_EMHDS      007          /* Here is device status */
#define PRI_EMRDT      011          /* Request Date/Time */
#define PRI_EMHDR      012          /* Here is date and time */
#define PRI_EMFLO      013          /* Flush output */
#define PRI_EMSNA      014          /* Send all (ttys) */
#define PRI_EMDSC      015          /* Dataset connect */
#define PRI_EMHUD      016          /* Hang up dataset */
#define PRI_EMLBE      017          /* Acknowledge line */
#define PRI_EMXOF      020          /* XOFF line */
#define PRI_EMXON      021          /* XON line */
#define PRI_EMHLS      022          /* Here is line speeds */
#define PRI_EMHLA      023          /* Here is line allocation */
#define PRI_EMRBI      024          /* Reboot information */
#define PRI_EMAKA      025          /* Ack ALL */
#define PRI_EMTDO      026          /* Turn device On/Off */
#define PRI_EMEDR      027          /* Enable/Disable line */
#define PRI_EMLDR      030          /* Load LP RAM */
#define PRI_EMLDV      031          /* Load LP VFU */

#define PRI_EMCTY      001          /* Device code for CTY */
#define PRI_EMDL1      002          /* DL11 */
#define PRI_EMDH1      003          /* DH11 #1 */
#define PRI_EMDLS      004          /* DLS (all ttys combined) */
#define PRI_EMLPT      005          /* Front end LPT */
#define PRI_EMCDR      006          /* CDR */
#define PRI_EMCLK      007          /* Clock */
#define PRI_EMFED      010          /* Front end device */
#define PRI_CTYDV      000          /* Line number for CTY */
#define NUM_DLS        5            /* Number of first DH Line */

#if KL_ITS
/* ITS Timesharing protocol locations */
#define ITS_DTEVER     0400         /* Protocol version and number of devices */
#define ITS_DTECHK     0401         /* Increment at 60Hz. Ten setom 2 times per second */
#define ITS_DTEINP     0402         /* Input from 10 to 11. Line #, Count */
#define ITS_DTEOUT     0403         /* Output from 10 to 11 Line #, Count */
#define ITS_DTELSP     0404         /* Line # to set speed of */
#define ITS_DTELPR     0405         /* Parameter */
#define ITS_DTEOST     0406         /* Line # to start output on */
#define ITS_DTETYI     0410         /* Received char (Line #, char) */
#define ITS_DTEODN     0411         /* Output done (Line #, buffer size) */
#define ITS_DTEHNG     0412         /* Hangup/dialup */
#endif

extern int32 tmxr_poll;
t_stat dte_devio(uint32 dev, uint64 *data);
t_addr dte_devirq(uint32 dev, t_addr addr);
void   dte_second(UNIT *uptr);
void   dte_primary(UNIT *uptr);
#if KL_ITS
void   dte_its(UNIT *uptr);
#endif
void   dte_transfer(UNIT *uptr);
void   dte_function(UNIT *uptr);
void   dte_input();
int    dte_start(UNIT *uptr);
int    dte_queue(int func, int dev, int dcnt, uint16 *data);
t_stat dtei_svc (UNIT *uptr);
t_stat dte_svc (UNIT *uptr);
t_stat dteo_svc (UNIT *uptr);
t_stat dtertc_srv(UNIT * uptr);
t_stat dte_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dte_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dte_reset (DEVICE *dptr);
t_stat dte_stop_os (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dte_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *dte_description (DEVICE *dptr);
extern uint64  SW;                                   /* Switch register */

CONST char *pri_name[] = { "(0)", "EM2EI", "EM2TI", "EMSTR", "EMLNC", "EMRDS", "(6)",
       "EMHDS", "(10)", "EMRDT", "EMHDR", "EMFLO", "EMSNA", "EMDSC", "EMHUD",
       "EMLBE", "EMXOF", "EMXON", "EMHLS", "EMHLA", "EMRBI", "EMAKA", "EMTDO",
       "EMEDR", "EMLDR", "EMLDV" };

#if KL_ITS
#define QITS         (cpu_unit[0].flags & UNIT_ITSPAGE)
#else
#define QITS         0
#endif

#define STATUS            u3
#define CNT               u4

extern uint32  eb_ptr;
static int32   rtc_tps = 60;
uint16         rtc_tick;
uint16         rtc_wait = 0;

struct _dte_queue {
    int         dptr;      /* Pointer to working item */
    uint16      cnt;       /* Number of bytes in packet */
    uint16      func;      /* Function code */
    uint16      dev;       /* Dev code */
    uint16      spare;     /* Dev code */
    uint16      dcnt;      /* Data count */
    uint16      data[258]; /* Data packet */
    uint16      sdev;      /* Secondary device code */
    uint16      sz;        /* Byte size */
} dte_in[32], dte_out[32];

int32 dte_in_ptr;
int32 dte_in_cmd;
int32 dte_out_ptr;
int32 dte_out_res;
int32 dte_base;            /* Base */
int32 dte_off;             /* Our offset */
int32 dte_dt10_off;        /* Offset to 10 deposit region */
int32 dte_et10_off;        /* Offset to 10 examine region */
int32 dte_et11_off;        /* Offset to 11 examine region */
int32 dte_proc_num;        /* Our processor number */

struct _buffer {
    int      in_ptr;     /* Insert pointer */
    int      out_ptr;    /* Remove pointer */
    char     buff[256];  /* Buffer */
} cty_in, cty_out;
int32 cty_done;

#define full(q)       ((((q)->in_ptr + 1) & 0xff) == (q)->out_ptr)
#define empty(q)      ((q)->in_ptr == (q)->out_ptr)
#define not_empty(q)  ((q)->in_ptr != (q)->out_ptr)
#define inco(q)       (q)->out_ptr = ((q)->out_ptr + 1) & 0xff
#define inci(q)       (q)->in_ptr = ((q)->in_ptr + 1) & 0xff

DIB dte_dib[] = {
    { DTE_DEVNUM|000, 1, dte_devio, dte_devirq},
};

MTAB dte_mod[] = {
    { UNIT_DUMMY, 0, NULL, "STOP", &dte_stop_os },
    { TT_MODE, TT_MODE_UC, "UC", "UC", &tty_set_mode },
    { TT_MODE, TT_MODE_7B, "7b", "7B", &tty_set_mode },
    { TT_MODE, TT_MODE_8B, "8b", "8B", &tty_set_mode },
    { TT_MODE, TT_MODE_7P, "7p", "7P", &tty_set_mode },
    {MTAB_XTD|MTAB_VDV, TYPE_RSX10, NULL, "RSX10",  &dte_set_type, NULL,
              NULL, "Sets DTE to RSX10 mode"},
    {MTAB_XTD|MTAB_VDV, TYPE_RSX20, "RSX20", "RSX20", &dte_set_type, &dte_show_type,
              NULL, "Sets DTE to RSX20 mode"},
    { 0 }
    };

UNIT dte_unit[] = {
    { UDATA (&dte_svc, TT_MODE_7B, 0), 100},
    { UDATA (&dteo_svc, TT_MODE_7B, 0), 100},
    { UDATA (&dtei_svc, TT_MODE_7B|UNIT_DIS, 0), 1000 },
    { UDATA (&dtertc_srv, UNIT_IDLE|UNIT_DIS, 0), 1000 }
    };

REG  dte_reg[] = {
    {SAVEDATA(IN, dte_in) },
    {SAVEDATA(OUT, dte_out) },
    {HRDATA(IN_PTR, dte_in_ptr, 32), REG_HRO},
    {HRDATA(IN_CMD, dte_in_cmd, 32), REG_HRO},
    {HRDATA(OUT_PTR, dte_out_ptr, 32), REG_HRO},
    {HRDATA(OUT_RES, dte_out_res, 32), REG_HRO},
    {HRDATA(BASE, dte_base, 32), REG_HRO},
    {HRDATA(OFF, dte_off, 32), REG_HRO},
    {HRDATA(DTOFF, dte_dt10_off, 32), REG_HRO},
    {HRDATA(ETOFF, dte_et10_off, 32), REG_HRO},
    {HRDATA(E1OFF, dte_et11_off, 32), REG_HRO},
    {HRDATA(PROC, dte_proc_num, 32), REG_HRO},
    {SAVEDATA(CTYIN, cty_in) },
    {SAVEDATA(CTYOUT, cty_out) },
    {HRDATA(DONE, cty_done, 8), REG_HRO},
    {HRDATAD(WRU, sim_int_char, 8, "interrupt character") },
    { 0 },
    };


DEVICE dte_dev = {
    "CTY", dte_unit, dte_reg, dte_mod,
    4, 10, 31, 1, 8, 8,
    NULL, NULL, &dte_reset,
    NULL, NULL, NULL, &dte_dib, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dte_help, NULL, NULL, &dte_description
    };



#ifndef NUM_DEVS_LP20
#define NUM_DEVS_LP20 0
#endif

#if (NUM_DEVS_LP20 > 0)

#define COL      u4
#define POS      u5
#define LINE     u6
#define LPST     us9
#define LPCNT    us10

#define EOFFLG   001      /* Tops 20 wants EOF */
#define HDSFLG   002      /* Tell Tops 20 The current device status */
#define ACKFLG   004      /* Post an acknowwledge message */
#define INTFLG   010      /* Send interrupt */
#define DELFLG   020      /* Previous character was delimiter */

#define MARGIN   6

#define UNIT_V_CT    (UNIT_V_UF + 0)
#define UNIT_UC      (1 << UNIT_V_CT)
#define UNIT_CT      (3 << UNIT_V_CT)



t_stat          lp20_svc (UNIT *uptr);
t_stat          lp20_reset (DEVICE *dptr);
t_stat          lp20_attach (UNIT *uptr, CONST char *cptr);
t_stat          lp20_detach (UNIT *uptr);
t_stat          lp20_setlpp(UNIT *, int32, CONST char *, void *);
t_stat          lp20_getlpp(FILE *, UNIT *, int32, CONST void *);
t_stat          lp20_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                         const char *cptr);
const char     *lp20_description (DEVICE *dptr);

char            lp20_buffer[134 * 3];

#define LP20_RAM_RAP  010000     /* RAM Parity */
#define LP20_RAM_INT  04000      /* Interrrupt bit */
#define LP20_RAM_DEL  02000      /* Delimiter bit */
#define LP20_RAM_TRN  01000      /* Translation bite */
#define LP20_RAM_PI   00400      /* Paper Instruction */
#define LP20_RAM_CHR  00377      /* Character translation */

uint16          lp20_vfu[256];
uint16          lp20_ram[256];
uint16          lp20_dvfu[] = {   /* Default VFU */
    /* 66 line page with 6 line margin */
    00377,    /* Line   0     8  7  6  5  4  3  2  1 */
    00220,    /* Line   1     8        5             */
    00224,    /* Line   2     8        5     3       */
    00230,    /* Line   3     8        5  4          */
    00224,    /* Line   4     8        5     3       */
    00220,    /* Line   5     8        5             */
    00234,    /* Line   6     8        5  4  3       */
    00220,    /* Line   7     8        5             */
    00224,    /* Line   8     8        5     3       */
    00230,    /* Line   9     8        5  4          */
    00264,    /* Line  10     8     6  5     3       */
    00220,    /* Line  11     8        5             */
    00234,    /* Line  12     8        5  4  3       */
    00220,    /* Line  13     8        5             */
    00224,    /* Line  14     8        5     3       */
    00230,    /* Line  15     8        5  4          */
    00224,    /* Line  16     8        5     3       */
    00220,    /* Line  17     8        5             */
    00234,    /* Line  18     8        5  4  3       */
    00220,    /* Line  19     8        5             */
    00364,    /* Line  20     8  7  6  5     3       */
    00230,    /* Line  21     8        5  4          */
    00224,    /* Line  22     8        5     3       */
    00220,    /* Line  23     8        5             */
    00234,    /* Line  24     8        5  4  3       */
    00220,    /* Line  25     8        5             */
    00224,    /* Line  26     8        5     3       */
    00230,    /* Line  27     8        5  4          */
    00224,    /* Line  28     8        5     3       */
    00220,    /* Line  29     8        5             */
    00276,    /* Line  30     8     6  5  4  3  2    */
    00220,    /* Line  31     8        5             */
    00224,    /* Line  32     8        5     3       */
    00230,    /* Line  33     8        5  4          */
    00224,    /* Line  34     8        5     3       */
    00220,    /* Line  35     8        5             */
    00234,    /* Line  36     8        5  4  3       */
    00220,    /* Line  37     8        5             */
    00224,    /* Line  38     8        5     3       */
    00230,    /* Line  39     8        5  4          */
    00364,    /* Line  40     8  7  6  5     3       */
    00220,    /* Line  41     8        5             */
    00234,    /* Line  42     8        5  4  3       */
    00220,    /* Line  43     8        5             */
    00224,    /* Line  44     8        5     3       */
    00230,    /* Line  45     8        5  4          */
    00224,    /* Line  46     8        5     3       */
    00220,    /* Line  47     8        5             */
    00234,    /* Line  48     8        5  4  3       */
    00220,    /* Line  49     8        5             */
    00264,    /* Line  50     8     6  5     3       */
    00230,    /* Line  51     8        5  4          */
    00224,    /* Line  52     8        5     3       */
    00220,    /* Line  53     8        5             */
    00234,    /* Line  54     8        5  4  3       */
    00220,    /* Line  55     8        5             */
    00224,    /* Line  56     8        5     3       */
    00230,    /* Line  57     8        5  4          */
    00224,    /* Line  58     8        5     3       */
    00220,    /* Line  59     8        5             */
    00020,    /* Line  60              5             */
    00020,    /* Line  61              5             */
    00020,    /* Line  62              5             */
    00020,    /* Line  63              5             */
    00020,    /* Line  64              5             */
    04020,    /* Line  65 12           5             */
   010000,    /* End of form */
};

struct _buffer lp20_queue;

/* LPT data structures

   lp20_dev      LPT device descriptor
   lp20_unit     LPT unit descriptor
   lp20_reg      LPT register list
*/

UNIT lp20_unit = {
    UDATA (&lp20_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 66), 100
    };

REG lp20_reg[] = {
   {BRDATA(BUFFER, lp20_buffer, 16, 8, sizeof(lp20_buffer)), REG_HRO},
   {BRDATA(VFU, lp20_vfu, 16, 16, (sizeof(lp20_vfu)/sizeof(uint16))), REG_HRO},
   {BRDATA(RAM, lp20_ram, 16, 16, (sizeof(lp20_ram)/sizeof(uint16))), REG_HRO},
   {SAVEDATA(QUEUE, lp20_queue) },
    { NULL }
};

MTAB lp20_mod[] = {
    {UNIT_CT, 0, "Lower case", "LC", NULL},
    {UNIT_CT, UNIT_UC, "Upper case", "UC", NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
        &lp20_setlpp, &lp20_getlpp, NULL, "Number of lines per page"},
    { 0 }
};

DEVICE lp20_dev = {
    "LP20", &lp20_unit, lp20_reg, lp20_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lp20_reset,
    NULL, &lp20_attach, &lp20_detach,
    NULL, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lp20_help, NULL, NULL, &lp20_description
};
#endif

#ifndef NUM_DEVS_TTY
#define NUM_DEVS_TTY 0
#endif

#if (NUM_DEVS_TTY > 0)

struct _buffer tty_out[NUM_LINES_TTY], tty_in[NUM_LINES_TTY];
TMLN     tty_ldsc[NUM_LINES_TTY] = { 0 };            /* Line descriptors */
TMXR     tty_desc = { NUM_LINES_TTY, 0, 0, tty_ldsc };
int32    tty_connect[NUM_LINES_TTY];
int32    tty_done[NUM_LINES_TTY];
int      tty_enable = 0;
extern int32 tmxr_poll;

t_stat ttyi_svc (UNIT *uptr);
t_stat ttyo_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);
t_stat tty_set_modem (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_show_modem (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tty_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tty_attach (UNIT *uptr, CONST char *cptr);
t_stat tty_detach (UNIT *uptr);
t_stat tty_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr);
const char *tty_description (DEVICE *dptr);

/* TTY data structures

   tty_dev      TTY device descriptor
   tty_unit     TTY unit descriptor
   tty_reg      TTY register list
*/

UNIT tty_unit[] = {
    { UDATA (&ttyi_svc, TT_MODE_7B+UNIT_IDLE+UNIT_DISABLE+UNIT_ATTABLE, 0), KBD_POLL_WAIT},
    { UDATA (&ttyo_svc, TT_MODE_7B+UNIT_IDLE+UNIT_DIS, 0), KBD_POLL_WAIT},
    };

REG tty_reg[] = {
    { DRDATA (TIME, tty_unit[0].wait, 24), REG_NZ + PV_LEFT },
    { SAVEDATA (OUT, tty_out) },
    { SAVEDATA (IN, tty_in) },
    { BRDATA (CONN, tty_connect, 8, 32, sizeof(tty_connect)/sizeof(int32)), REG_HRO },
    { BRDATA (DONE, tty_done, 8, 32, sizeof(tty_done)/sizeof(int32)), REG_HRO },
    { ORDATA (EN, tty_enable, 1), REG_HRO },
    { 0 }
    };

MTAB tty_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &tty_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &tty_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &tty_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &tty_desc, "Display multiplexer statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &tty_setnl, &tmxr_show_lines, (void *) &tty_desc, "Set number of lines" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &tty_set_log, NULL, (void *)&tty_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG",
        &tty_set_nolog, NULL, (void *)&tty_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &tty_show_log, (void *)&tty_desc, "Display logging for all lines" },
    { 0 }
    };

DEVICE tty_dev = {
    "TTY", tty_unit, tty_reg, tty_mod,
    2, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &tty_reset,
    NULL, &tty_attach, &tty_detach,
    NULL, DEV_MUX | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &tty_help, NULL, NULL, &tty_description
    };
#endif


t_stat dte_devio(uint32 dev, uint64 *data) {
     uint32     res;
     switch(dev & 3) {
     case CONI:
        *data = (uint64)(dte_unit[0].STATUS) & RMASK;
        sim_debug(DEBUG_CONI, &dte_dev, "CTY %03o CONI %06o\n", dev, (uint32)*data);
        break;
     case CONO:
         res = (uint32)(*data & RMASK);
         clr_interrupt(dev);
         if (res & DTE_PIENB) {
             dte_unit[0].STATUS &= ~(DTE_PIA|DTE_PIE);
             dte_unit[0].STATUS |= res & (DTE_PIA|DTE_PIE);
         }
         if (res & DTE_CO11CL)
             dte_unit[0].STATUS &= ~(DTE_11DN|DTE_11ER);
         if (res & DTE_CO10CL) {
             dte_unit[0].STATUS &= ~(DTE_10DN|DTE_10ER);
             dte_start(&dte_unit[0]);
         }
         if (res & DTE_CO10DB)
             dte_unit[0].STATUS &= ~(DTE_10DB);
         if (res & DTE_CO11CR)
             dte_unit[0].STATUS &= ~(DTE_11RELD);
         if (res & DTE_CO11SR)
             dte_unit[0].STATUS |= (DTE_11RELD);
         if (res & DTE_CO11DB) {
             sim_debug(DEBUG_CONO, &dte_dev, "CTY Ring 11 DB\n");
             dte_unit[0].STATUS |= DTE_11DB;
             sim_activate(&dte_unit[0], 200);
         }
         if (dte_unit[0].STATUS & (DTE_10DB|DTE_11DN|DTE_10DN|DTE_11ER|DTE_10ER))
             set_interrupt(dev, dte_unit[0].STATUS);
         sim_debug(DEBUG_CONO, &dte_dev, "CTY %03o CONO %06o %06o\n", dev,
                      (uint32)*data, PC);
         break;
     case DATAI:
         sim_debug(DEBUG_DATAIO, &dte_dev, "CTY %03o DATAI %06o\n", dev,
                      (uint32)*data);
         break;
    case DATAO:
         sim_debug(DEBUG_DATAIO, &dte_dev, "CTY %03o DATAO %06o\n", dev,
                      (uint32)*data);
         if (*data == 01365) {
             dte_unit[0].STATUS |= DTE_SEC|DTE_10ER;
             dte_unit[0].STATUS &= ~(DTE_10DB|DTE_IND|DTE_11DB);
             break;
         }
         dte_unit[0].CNT = (*data & (DTE_TO10IB|DTE_TO10BC));
         dte_unit[0].STATUS |= DTE_TO11;
         sim_activate(&dte_unit[0], 10);
         break;
    }
    return SCPE_OK;
}

/* Handle KL style interrupt vectors */
t_addr
dte_devirq(uint32 dev, t_addr addr) {
    return 0142;
}

/* Handle TO11 interrupts */
t_stat dte_svc (UNIT *uptr)
{
    /* Did the 10 knock? */
    if (uptr->STATUS & DTE_11DB) {
        /* If in secondary mode, do that protocol */
        if (uptr->STATUS & DTE_SEC)
            dte_second(uptr);
        else
            dte_primary(uptr);   /* Retrieve data */
    } else if (uptr->STATUS & DTE_TO11) {
        /* Does 10 want us to send it what we have? */
        dte_transfer(uptr);
    }
    return SCPE_OK;
}

/* Handle secondary protocol */
void dte_second(UNIT *uptr) {
    uint64   word;
    int32    ch;
    uint32   base = 0;

#if KI_22BIT
#if KL_ITS
    if (!QITS)
#endif
    base = eb_ptr;
#endif
    /* read command */
    word = M[SEC_DTCMD + base];
#if KL_ITS
    if (word == 0 && QITS && (uptr->STATUS & ITS_ON) != 0) {
        dte_its(uptr);
        uptr->STATUS &= ~DTE_11DB;
        return;
    }
#endif
    /* Do it */
    sim_debug(DEBUG_DETAIL, &dte_dev, "CTY secondary %012llo\n", word);
    switch(word & SEC_CMDMSK) {
    default:
    case SEC_MONO:  /* Ouput character in monitor mode */
         ch = (int32)(word & 0177);
         if (full(&cty_out)) {
            sim_activate(uptr, 200);
            return;
         }
         if (ch != 0) {
             cty_out.buff[cty_out.in_ptr] = ch & 0x7f;
             inci(&cty_out);
             sim_activate(&dte_unit[1], 200);
         }
         M[SEC_DTCHR + base] = ch;
         M[SEC_DTMTD + base] = FMASK;
         break;

     case SEC_SETPRI:
enter_pri:
         if (Mem_examine_word(0, 0, &word))
             break;
         dte_proc_num = (word >> 24) & 037;
         dte_base = dte_proc_num + 1;
         dte_off = dte_base + (word & 0177777);
         dte_dt10_off = 16;
         dte_et10_off = dte_dt10_off + 16;
         dte_et11_off = dte_base + 16;
         uptr->STATUS &= ~DTE_SEC;
         dte_in_ptr = dte_out_ptr = 0;
         dte_in_cmd = dte_out_res = 0;
         cty_done = 0;
         /* Start input process */
         M[SEC_DTCMD + base] = 0;
         M[SEC_DTFLG + base] = FMASK;
         uptr->STATUS &= ~DTE_11DB;
         return;

     case SEC_SETDDT: /* Read character from console */
         if (empty(&cty_in)) {
             M[SEC_DTF11 + base] = 0;
             M[SEC_DTMTI + base] = FMASK;
             break;
         }
         ch = cty_in.buff[cty_in.out_ptr];
         inco(&cty_in);
         M[SEC_DTF11 + base] = 0177 & ch;
         M[SEC_DTMTI + base] = FMASK;
         break;

     case SEC_CLRDDT: /* Clear DDT input mode */
         uptr->STATUS &= ~DTE_MON;
         break;

     case SEC_MONON:
         uptr->STATUS |= DTE_MON;
         break;

     case SEC_RDSW:  /* Read switch register */
         M[SEC_DTSWR + base] = SW;
         M[SEC_DTF11 + base] = SW;
         break;

     case SEC_PGMCTL: /* Program control: Used by KLDCP */
         switch(word) {
         case SEC_ENDPASS:
         case SEC_LOOKUP:
         case SEC_RDWRD:
         case SEC_RDBYT:
              break;
         case SEC_ESEC:
              goto enter_pri;
         case SEC_EPRI:
         case SEC_ERTM:
              break;
         }
         break;

     case SEC_CLKCTL: /* Clock control: Used by KLDCP */
         switch(word) {
         case SEC_CLKOFF:
              dte_unit[3].STATUS &= ~SEC_CLK;
              break;
         case SEC_CLKWT:
              rtc_wait = (uint16)(M[SEC_DTT11 + base] & 0177777);
              /* Fall Through */

         case SEC_CLKON:
              dte_unit[3].STATUS |= SEC_CLK;
              rtc_tick = 0;
              break;
         case SEC_CLKRD:
              M[SEC_DTF11+base] = rtc_tick;
              break;
         }
         break;
     }
     /* Acknowledge command */
     M[SEC_DTCMD + base] = 0;
     M[SEC_DTFLG + base] = FMASK;
     uptr->STATUS &= ~DTE_11DB;
     if (dte_dev.flags & TYPE_RSX20) {
         uptr->STATUS |= DTE_10DB;
         set_interrupt(DTE_DEVNUM, dte_unit[0].STATUS);
     }
}

#if KL_ITS
/* Process ITS Ioeleven locations */
void dte_its(UNIT *uptr) {
     uint64     word;
     char       ch;
     uint16     data;
     int        cnt;
     int        ln;

     /* Check for input Start */
     word = M[ITS_DTEINP];
     if ((word & SMASK) == 0) {
         M[ITS_DTEINP] = FMASK;
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEINP = %012llo\n", word);
     }
     /* Check for output Start */
     word = M[ITS_DTEOUT];
     if ((word & SMASK) == 0) {
         cnt = word & 017777;
         ln = ((word >> 18) & 077) - 1;
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEOUT = %012llo\n", word);
         while (cnt > 0) {
             if (ln < 0) {
                 if (full(&cty_out))
                    return;
                 if (!Mem_read_byte(0, &data, 1))
                    return;
                 ch = data & 0177;
                 sim_debug(DEBUG_DETAIL, &dte_dev, "CTY queue %x\n", ch);
                 cty_out.buff[cty_out.in_ptr] = ch;
                 inci(&cty_out);
                 cnt--;
                 if (! sim_is_active(&dte_unit[1]))
                     sim_activate(&dte_unit[1], 50);
#if (NUM_DEVS_TTY > 0)
             } else {
                 struct _buffer *otty = &tty_out[ln];
                 if (full(otty))
                    return;
                 if (!Mem_read_byte(0, &data, 1))
                    return;
                 ch = data & 0177;
                 sim_debug(DEBUG_DETAIL, &dte_dev, "TTY queue %x %d\n", ch, ln);
                 otty->buff[otty->in_ptr] = ch;
                 inci(otty);
                 cnt--;
#endif
             }
         }
         M[ITS_DTEOUT] = FMASK;
         uptr->STATUS |= DTE_11DN;
         set_interrupt(DTE_DEVNUM, uptr->STATUS);
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEOUT = %012llo\n", word);
     }
     /* Check for line speed */
     word = M[ITS_DTELSP];
     if ((word & SMASK) == 0) {  /* Ready? */
         M[ITS_DTELSP] = FMASK;
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTELSP = %012llo %012llo\n", word, M[ITS_DTELPR]);
     }
     dte_input();
     /* Check for output Start */
     word = M[ITS_DTEOST];
     if ((word & SMASK) == 0) {
         if (word == 0)
             cty_done++;
#if (NUM_DEVS_TTY > 0)
         else if (word > 0 && word < (uint64)tty_desc.lines) {
            tty_done[word-1] = 1;
         }
#endif
         M[ITS_DTEOST] = FMASK;
         sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEOST = %012llo\n", word);
     }
}
#endif

/* Handle primary protocol */
void dte_primary(UNIT *uptr) {
    uint64   word, iword;
    int      s;
    int      cnt;
    struct   _dte_queue *in;
    uint16   data1, *dp;

    if ((uptr->STATUS & DTE_11DB) == 0)
        return;

    /* Check if there is room for another packet */
    if (((dte_in_ptr + 1) & 0x1f) == dte_in_cmd) {
        /* If not reschedule ourselves */
        sim_activate(uptr, 100);
        return;
    }
    uptr->STATUS &= ~(DTE_11DB);
    clr_interrupt(DTE_DEVNUM);
    /* Check status word to see if valid */
    if (Mem_examine_word(0, dte_et11_off + PRI_CMTW_STS, &word)) {
         uint32   base;
error:
         base = 0;
#if KI_22BIT
#if KL_ITS
         if (!QITS)
#endif
         base = eb_ptr;
#endif
         /* If we can't read it, go back to secondary */
         M[SEC_DTFLG + base] = FMASK;
         uptr->STATUS |= DTE_SEC;
         uptr->STATUS &= ~DTE_11DB;
         if (dte_dev.flags & TYPE_RSX20) {
             uptr->STATUS |= DTE_10DB;
             set_interrupt(DTE_DEVNUM, dte_unit[0].STATUS);
         }
         sim_debug(DEBUG_DETAIL, &dte_dev, "DTE: error %012llo\n", word);
         return;
    }

    if ((word & PRI_CMT_QP) == 0) {
        goto error;
    }
    in = &dte_in[dte_in_ptr];
    /* Check if indirect */
    if ((word & PRI_CMT_IP) != 0) {
        /* Transfer from 10 */
        if ((uptr->STATUS & DTE_IND) == 0) {
            fprintf(stderr, "DTE out of sync\n\r");
            return;
        }
        /* Get size of transfer */
        if (Mem_examine_word(0, dte_et11_off + PRI_CMTW_CNT, &iword))
            goto error;
        sim_debug(DEBUG_EXP, &dte_dev, "DTE: count: %012llo\n", iword);
        in->dcnt = (uint16)(iword & 0177777);
        /* Read in data */
        dp = &in->data[0];
        for (cnt = in->dcnt; cnt > 0; cnt --) {
            /* Read in data */
            s = Mem_read_byte(0, dp, 0);
            if (s == 0)
               goto error;
            in->sz = s;
            sim_debug(DEBUG_DATA, &dte_dev,
                   "DTE: Read Idata: %06o %03o %03o %06o cnt=%o\n",
                    *dp, *dp >> 8, *dp & 0377,
                    ((*dp & 0377) << 8) | ((*dp >> 8) & 0377), cnt);
            dp++;
            if (s <= 8)
               cnt--;
        }
        uptr->STATUS &= ~DTE_IND;
        dte_in_ptr = (dte_in_ptr + 1) & 0x1f;
    } else {
        /* Transfer from 10 */
        in->dptr = 0;
        in->dcnt = 0;
        /* Read in count */
        if (!Mem_read_byte(0, &data1, 0))
            goto error;
        in->cnt = data1;
        cnt = in->cnt-2;
        if (!Mem_read_byte(0, &data1, 0))
            goto error;
        in->func = data1;
        cnt -= 2;
        if (!Mem_read_byte(0, &data1, 0))
            goto error;
        in->dev = data1;
        cnt -= 2;
        if (!Mem_read_byte(0, &data1, 0))
            goto error;
        in->spare = data1;
        cnt -= 2;
        sim_debug(DEBUG_DATA, &dte_dev, "DTE: Read CMD: %o c=%o f=%o %s d=%o\n",
                  dte_in_ptr, in->cnt, in->func,
                  ((in->func & 0377) > PRI_EMLDV)?"***":
                       pri_name[in->func & 0377], in->dev);
        dp = &in->data[0];
        for (; cnt > 0; cnt -=2) {
            /* Read in data */
            if (!Mem_read_byte(0, dp, 0))
               goto error;
            sim_debug(DEBUG_DATA, &dte_dev, "DTE: Read data: %06o %03o %03o\n",
                          *dp, *dp >> 8, *dp & 0377);
            dp++;
            in->dcnt += 2;
        }
        if (in->func & PRI_IND_FLG) {
            uptr->STATUS |= DTE_IND;
            in->dcnt = in->data[0];
            in->sdev = (in->dcnt >> 8) & 0377;
            in->dcnt &= 0377;
            word |= PRI_CMT_TOT;
            if (Mem_deposit_word(0, dte_dt10_off + PRI_CMTW_STS, &word))
                goto error;
        } else {
            dte_in_ptr = (dte_in_ptr + 1) & 0x1f;
        }
    }
    word &= ~PRI_CMT_TOT;
    if (Mem_deposit_word(0, dte_dt10_off + PRI_CMTW_STS, &word))
        goto error;
    uptr->STATUS |= DTE_11DN;
    set_interrupt(DTE_DEVNUM, uptr->STATUS);
    dte_function(uptr);
}

/* Process primary protocol packets */
void
dte_function(UNIT *uptr)
{
    uint16    data1[32];
    int32     ch;
    struct _dte_queue *cmd;
    int       func;
    int       dev;

    /* Check if queue is empty */
    while (dte_in_cmd != dte_in_ptr) {
        if (((dte_out_res + 1) & 0x1f) == dte_out_ptr) {
            sim_debug(DEBUG_DATA, &dte_dev, "DTE: func out full %d %d\n",
                          dte_out_res, dte_out_ptr);
            return;
        }
        cmd = &dte_in[dte_in_cmd];
        dev = cmd->dev & 0377;
        func = cmd->func & 0377;
        sim_debug(DEBUG_DATA, &dte_dev,
                "DTE: func %o %02o %s dev %o cnt %d dcnt %d\n",
                dte_in_cmd, func, (func > PRI_EMLDV) ? "***" :  pri_name[func],
                cmd->dev, cmd->dcnt, cmd->dptr );
        switch (func) {
        case PRI_EM2EI:            /* Initial message to 11 */
               data1[0] = PRI_CTYDV;
               if (dte_queue(PRI_EM2TI, PRI_EMCTY, 1, data1) == 0)
                   return;
#if (NUM_DEVS_LP20 > 0)
               data1[0] = 140;
               if (dte_queue(PRI_EMHLA, PRI_EMLPT, 1, data1) == 0)
                   return;
#endif
               data1[0] = 0;
               if (dte_queue(PRI_EMAKA, PRI_EMCLK, 1, data1) == 0)
                   return;
               break;

        case PRI_EM2TI:            /* Replay to initial message. */
        case PRI_EMLBE:            /* Acknowledge line */
               /* Should never get these */
               break;

        case PRI_EMHDR:            /* Here is date and time */
               /* Ignore this function */
               break;

        case PRI_EMRDT:            /* Request Date/Time */
               {
                   time_t t = sim_get_time(NULL);
                   struct tm *tm = localtime(&t);
                   int yr = tm->tm_year + 1900;
                   int tim = (((tm->tm_hour * 60) + tm->tm_min) * 60) +
                               tm->tm_sec;
                   data1[0] = 0177777;
                   data1[1] = ((yr & 0377) << 8) | ((yr >> 8) & 0377);
                   data1[2] = (tm->tm_mon) + ((tm->tm_mday - 1) << 8);
                   data1[3] = (((tm->tm_wday + 6) % 7)) +
                               (tm->tm_isdst ? 0200 << 8 : 0);
                   tim >>= 1;
                   data1[4] = ((tim & 0377) << 8) | ((tim >> 8) & 0377);
                   if (dte_queue(PRI_EMHDR | PRI_IND_FLG, PRI_EMCLK, 6, data1) == 0)
                       return;
               }
               break;

        case PRI_EMSTR:            /* String data */

#if (NUM_DEVS_LP20 > 0)
               /* Handle printer data */
               if (dev == PRI_EMLPT) {
                   uptr->LPST &= ~(EOFFLG);
                   if (!sim_is_active(&lp20_unit))
                       sim_activate(&lp20_unit, 1000);
                   while (cmd->dptr < cmd->dcnt) {
                       ch = (int32)(cmd->data[cmd->dptr >> 1]);
                       if ((cmd->dptr & 1) == 0)
                           ch >>= 8;
                       ch &= 0177;
                       if (full(&lp20_queue))
                          return;
                       lp20_queue.buff[lp20_queue.in_ptr] = ch;
                       inci(&lp20_queue);
                       cmd->dptr++;
                   }
                   if (cmd->dptr != cmd->dcnt)
                       return;
                   sim_debug(DEBUG_DETAIL, &dte_dev, "LP20 done\n");
                   break;
               }
#endif

               /* Handle terminal data */
               if (dev == PRI_EMDLS) {
                   int   ln = cmd->sdev;
                   struct _buffer *otty;
                   if (ln == PRI_CTYDV)
                       goto cty;
#if (NUM_DEVS_TTY > 0)
                   ln -= NUM_DLS;
                   if (ln < 0 || ln >= tty_desc.lines)
                       break;
                   otty = &tty_out[ln];
                   if (cmd->sz > 8)
                      cmd->dcnt += cmd->dcnt;
                   while (cmd->dptr < cmd->dcnt) {
                       ch = (int32)(cmd->data[cmd->dptr >> 1]);
                       if ((cmd->dptr & 1) == 0)
                           ch >>= 8;
                       ch &= 0177;
                       if (ch != 0) {
                           if (full(otty))
                              return;
                           otty->buff[otty->in_ptr] = ch;
                           inci(otty);
                           sim_debug(DEBUG_DATA, &dte_dev, "TTY queue %o %d\n",
                                  ch, ln);
                       }
                       cmd->dptr++;
                   }
                   if (cmd->dptr != cmd->dcnt)
                       return;
#endif
                   break;
               }

               if (dev == PRI_EMCTY) {
cty:
                   sim_activate(&dte_unit[1], 100);
                   data1[0] = 0;
                   if (cmd->sz > 8)
                       cmd->dcnt += cmd->dcnt;
                   while (cmd->dptr < cmd->dcnt) {
                        ch = (int32)(cmd->data[cmd->dptr >> 1]);
                        if ((cmd->dptr & 1) == 0)
                            ch >>= 8;
                        ch &= 0177;
                        if (ch != 0) {
                            ch = sim_tt_outcvt( ch, TT_GET_MODE(uptr->flags));
                            if (full(&cty_out))
                                return;
                            cty_out.buff[cty_out.in_ptr] = (char)(ch & 0xff);
                            inci(&cty_out);
                            sim_debug(DEBUG_DATA, &dte_dev,"CTY queue %o\n",ch);
                        }
                        cmd->dptr++;
                   }
                   if (cmd->dptr != cmd->dcnt)
                       return;
               }
               break;

        case PRI_EMSNA:            /* Send all (ttys) */
               /* Handle terminal data */
               if (dev == PRI_EMDLS || dev == PRI_EMCTY) {
                   struct _buffer *otty;
                   int    ln;
                   while (cmd->dptr < cmd->dcnt) {
                       ch = (int32)(cmd->data[cmd->dptr >> 1]);
                       if ((cmd->dptr & 1) == 0)
                           ch >>= 8;
                       ch &= 0177;
                       if (ch != 0) {
                           sim_debug(DEBUG_DATA, &dte_dev, "SNA queue %o\n", ch);
                           ch = sim_tt_outcvt( ch, TT_GET_MODE(uptr->flags));
                           if (!(full(&cty_out))) {
                               cty_out.buff[cty_out.in_ptr] = (char)(ch & 0xff);
                               inci(&cty_out);
                           }
#if (NUM_DEVS_TTY > 0)
                           for(ln = 0; ln <= tty_desc.lines; ln++) {
                               otty = &tty_out[ln];
                               if (!(full(otty))) {
                                   otty->buff[otty->in_ptr] = ch;
                                   inci(otty);
                               }
                           }
#endif
                       }
                       cmd->dptr++;
                   }
                   if (cmd->dptr != cmd->dcnt)
                       return;
                   data1[0] = 0;
               }
               break;

        case PRI_EMLNC:            /* Line-Char */
               if (dev == PRI_EMDLS) {
                   sim_activate(&dte_unit[1], 100);
                   while (cmd->dptr < cmd->dcnt) {
                        int ln;
                        ch = (int32)(cmd->data[cmd->dptr >> 1]);
                        ln = (ch >> 8);
                        ch &= 0177;
                        if (ch != 0 && ln == PRI_CTYDV) {
                            ch = sim_tt_outcvt( ch, TT_GET_MODE(uptr->flags));
                            cty_out.buff[cty_out.in_ptr] = (char)(ch & 0xff);
                            inci(&cty_out);
                            if (((cty_out.in_ptr + 1) & 0xff) == cty_out.out_ptr)
                                return;
                            sim_debug(DEBUG_DATA, &dte_dev, "CTY queue %o\n", ch);
                        } else
                        if (ch != 0 && ln >= NUM_DLS && ln <= tty_desc.lines) {
                            struct _buffer *otty;
                            ln -= NUM_DLS;
                            otty = &tty_out[ln];
                            if (full(otty))
                                return;
                            otty->buff[otty->in_ptr] = ch;
                            inci(otty);
                            sim_debug(DEBUG_DATA, &dte_dev, "TTY queue %o %d\n", ch, ln);
                        }
                        cmd->dptr+=2;
                   }
                   if (cmd->dptr != cmd->dcnt)
                       return;
               }
               break;

        case PRI_EMOPS:
#if (NUM_DEVS_LP20 > 0)
               if (dev == PRI_EMLPT) {
                  lp20_unit.LINE = 0;
               }
#endif
               break;

        case PRI_EMRDS:            /* Request device status */
               if (dev == PRI_EMLPT) {
                   if (cmd->data[0] != 0) {
                      data1[0] = 2 << 8;
                      data1[1] = 0;
                      data1[2] = 0;
                      if (dte_queue(PRI_EMHDS+PRI_IND_FLG, PRI_EMLPT,
                                         3, data1) == 0)
                          return;
                   } else {
#if (NUM_DEVS_LP20 > 0)
                      lp20_unit.LPST |= HDSFLG;
                      if (!sim_is_active(&lp20_unit))
                          sim_activate(&lp20_unit, 1000);
#else
                      data1[0] = 2 << 8;
                      data1[1] = 0;
                      data1[2] = 0;
                      if (dte_queue(PRI_EMHDS+PRI_IND_FLG, PRI_EMLPT,
                                         3, data1) == 0)
                          return;
#endif
                   }
               }
               if (dev == PRI_EMCTY) {
                   data1[0] = 0;
                   data1[1] = 0;
                   if (dte_queue(PRI_EMHDS+PRI_IND_FLG, PRI_EMCTY,
                                         3, data1) == 0)
                       return;
               }
               if (dev == PRI_EMDH1) {
                   data1[0] = 0;
                   data1[1] = 0;
                   if (dte_queue(PRI_EMHDS+PRI_IND_FLG, PRI_EMDH1,
                                         3, data1) == 0)
                       return;
               }
               break;

        case PRI_EMHDS:            /* Here is device status */
#if (NUM_DEVS_LP20 > 0)
               if (dev == PRI_EMLPT) {
                   sim_debug(DEBUG_DETAIL, &dte_dev, "LPT HDS %06o %06o %06o\n",
                            cmd->data[0], cmd->data[1], cmd->data[2]);
                   if (cmd->data[0] & 040) {
                       lp20_unit.LPST |= EOFFLG;
                       lp20_unit.LPCNT = 0;
                   }
                   lp20_unit.LPST |= HDSFLG;
                   sim_debug(DEBUG_DETAIL, &dte_dev, "LPT HDS %06o \n",
                              lp20_unit.LPST);
                   if (!sim_is_active(&lp20_unit))
                       sim_activate(&lp20_unit, 1000);
               }
#endif
               break;
        case PRI_EMLDV:            /* Load LP VFU */
#if (NUM_DEVS_LP20 > 0)
               if (dev == PRI_EMLPT) {
                   int ln = lp20_unit.LPCNT;
                   while (cmd->dptr < cmd->dcnt) {
                        uint16 d = cmd->data[cmd->dptr++];
                        if (d == (0357 << 8))
                            lp20_vfu[ln++] = 010000; /* Signal end of page */
                        else
                            lp20_vfu[ln++] = ((d >> 8) & 077)|((d <<6) & 07700);
                   }
                   lp20_unit.LPCNT = ln;
                   for (ln = 0; ln < 256; ln++)
                      sim_debug(DEBUG_DETAIL, &lp20_dev,
                                "LP20 VFU %02d => %04o\n", ln, lp20_vfu[ln]);
                   data1[0] = 0;
                   if (dte_queue(PRI_EMLBE, PRI_EMLPT, 1, data1) == 0)
                       sim_activate(uptr, 1000);
               }
#endif
               break;

        case PRI_EMLDR:            /* Load LP RAM */
#if (NUM_DEVS_LP20 > 0)
               if (dev == PRI_EMLPT) {
                   int ln = lp20_unit.LPCNT;
                   for (;cmd->dptr < cmd->dcnt; cmd->dptr++, ln++) {
                        if (ln < 256)
                            lp20_ram[ln] = cmd->data[cmd->dptr];
                   }
                   lp20_unit.LPCNT = ln;
                   for (ln = 0; ln < 256; ln++)
                      sim_debug(DEBUG_DETAIL, &lp20_dev,
                              "LP20 RAM %02x => %04x\n", ln, lp20_ram[ln]);
                   data1[0] = 0;
                   if (dte_queue(PRI_EMLBE, PRI_EMLPT, 1, data1) == 0)
                       sim_activate(uptr, 1000);
               }
#endif
               break;


        case PRI_EMFLO:            /* Flush output */
#if (NUM_DEVS_TTY > 0)
               if (dev == PRI_EMDLS) {
                  int   ln = cmd->data[0] - NUM_DLS;

                  sim_debug(DEBUG_DETAIL, &dte_dev, "Flush out %d %o\n",
                                      ln, cmd->data[0]);
                  if (ln == (NUM_DLS - PRI_CTYDV))
                      cty_out.in_ptr = cty_out.out_ptr = 0;
                  else
                      tty_out[ln].in_ptr = tty_out[ln].out_ptr = 0;
                  data1[0] = (ln + NUM_DLS) | (PRI_EMDLS << 8);
                  if (dte_queue(PRI_EMLBE, PRI_EMDLS, 1, data1) == 0)
                       return;
               }
#endif
#if (NUM_DEVS_LP20 > 0)
               if ((cmd->dev & 0377) == PRI_EMLPT) {
                  data1[0] = cmd->data[0];
                  if (dte_queue(PRI_EMLBE, PRI_EMLPT, 1, data1) == 0)
                       return;
               }
#endif
               break;

        case PRI_EMDSC:            /* Dataset connect */
               break;

        case PRI_EMHUD:            /* Hang up dataset */
#if (NUM_DEVS_TTY > 0)
               if (dev == PRI_EMDLS) {
                  int   ln = cmd->sdev - NUM_DLS;
                  if (ln >= 0) {
                      TMLN  *lp = &tty_ldsc[ln];
                      tmxr_linemsg (lp, "\r\nLine Hangup\r\n");
                      tmxr_reset_ln(lp);
                      tty_connect[ln] = 0;
                  }
               }
               break;

        case PRI_EMXOF:            /* XOFF line */
               if (dev == PRI_EMDLS) {
                  int   ln = cmd->sdev - NUM_DLS;
                  if (ln >= 0) {
                      tty_ldsc[ln].rcve = 0;
                  }
               }
               break;

        case PRI_EMXON:            /* XON line */
               if (dev == PRI_EMDLS) {
                  int   ln = cmd->sdev - NUM_DLS;
                  if (ln >= 0) {
                      tty_ldsc[ln].rcve = 1;
                  }
               }
               break;

        case PRI_EMHLS:            /* Here is line speeds */
               if (dev == PRI_EMDLS) {
                  int   ln = cmd->sdev - NUM_DLS;
                  sim_debug(DEBUG_DETAIL, &tty_dev, "HDL %o o=%d i=%d %o\n",
                            ln, cmd->data[0], cmd->data[1], cmd->data[2]);
               }
               break;

        case PRI_EMHLA:            /* Here is line allocation */
        case PRI_EMRBI:            /* Reboot information */
        case PRI_EMAKA:            /* Ack ALL */
        case PRI_EMTDO:            /* Turn device On/Off */
               break;

        case PRI_EMEDR:            /* Enable/Disable line */
               if (cmd->dev == PRI_EMDH1) {
                   /* Zero means enable, no-zero means disable */
                   tty_enable = !((cmd->data[0] >> 8) & 0xff);
                   sim_debug(DEBUG_DETAIL, &dte_dev, "CTY enable %x\n",
                                 tty_enable);
                   if (tty_enable) {
                      sim_activate(&tty_unit[0], 1000);
                      sim_activate(&tty_unit[1], 1000);
                   } else {
                      sim_cancel(&tty_unit[0]);
                      sim_cancel(&tty_unit[1]);
                   }
               }
               break;
#endif
        default:
               break;
        }
        /* Mark command as finished */
        cmd->cnt = 0;
        dte_in_cmd = (dte_in_cmd + 1) & 0x1F;
    }
}

/*
 * Handle primary protocol,
 * Send to 10 when requested.
 */
void dte_transfer(UNIT *uptr) {
    uint16   cnt;
    uint16   scnt;
    struct   _dte_queue *out;
    uint16   *dp;

    /* Check if Queue empty */
    if (dte_out_res == dte_out_ptr)
        return;

    out = &dte_out[dte_out_ptr];
    uptr->STATUS &= ~DTE_TO11;
    clr_interrupt(DTE_DEVNUM);

    /* Compute how much 10 wants us to send */
    scnt = ((uptr->CNT ^ DTE_TO10BC) + 1) & DTE_TO10BC;
    /* Check if indirect */
    if ((uptr->STATUS & DTE_SIND) != 0) {
       /* Transfer indirect */
       cnt = out->dcnt;
       dp = &out->data[0];
       if (cnt > scnt)  /* Only send as much as we are allowed */
          cnt = scnt;
       for (; cnt > 0; cnt -= 2) {
           sim_debug(DEBUG_DATA, &dte_dev, "DTE: Send Idata: %06o %03o %03o\n",
                          *dp, *dp >> 8, *dp & 0377);
           if (Mem_write_byte(0, dp) == 0)
              goto error;
           dp++;
       }
       uptr->STATUS &= ~DTE_SIND;
    } else {
        sim_debug(DEBUG_DATA, &dte_dev, "DTE: %d %d send CMD: [%o] %o %o %o\n",
                dte_out_ptr, dte_out_res, scnt, out->cnt, out->func, out->dev);
       /* Get size of packet */
       cnt = out->cnt;
       if ((out->func & PRI_IND_FLG) == 0)
           cnt += out->dcnt;
       /* If it will not fit, request indirect */
       if (cnt > scnt) {  /* If not enough space request indirect */
           out->func |= PRI_IND_FLG;
           cnt = scnt;
       }
       /* Write out header */
       if (!Mem_write_byte(0, &cnt))
          goto error;
       if (!Mem_write_byte(0, &out->func))
          goto error;
       cnt -= 2;
       if (!Mem_write_byte(0, &out->dev))
          goto error;
       cnt -= 2;
       if (!Mem_write_byte(0, &out->spare))
           goto error;
       cnt -= 2;
       if (out->func & PRI_IND_FLG) {
           uint16 dwrd = out->dcnt;
           sim_debug(DEBUG_DATA, &dte_dev, "DTE: Indirect %o %o\n", cnt,
                              out->dcnt);
           dwrd |= (out->sdev << 8);
           if (!Mem_write_byte(0, &dwrd))
              goto error;
           uptr->STATUS |= DTE_SIND;
           goto done;
       }
       cnt -= 2;
       dp = &out->data[0];
       for (; cnt > 0; cnt -= 2) {
           sim_debug(DEBUG_DATA, &dte_dev, "DTE: Send data: %06o %03o %03o\n",
                          *dp, *dp >> 8, *dp & 0377);
           if (!Mem_write_byte(0, dp))
              goto error;
           dp++;
       }
    }
    out->cnt = 0;
    dte_out_ptr = (dte_out_ptr + 1) & 0x1f;
done:
    uptr->STATUS |= DTE_10DN;
    set_interrupt(DTE_DEVNUM, uptr->STATUS);
error:
    return;
}

/* Process input from CTY and TTY's to 10.  */
void
dte_input()
{
    uint16  data1;
    uint16  dataq[32];
    int     n;
    int     ln;
    int     save_ptr;
    char    ch;
    UNIT    *uptr = &dte_unit[0];

#if KL_ITS
    if (QITS && (uptr->STATUS & ITS_ON) != 0) {
       uint64   word;
       word = M[ITS_DTEODN];
       /* Check if ready for output done */
       sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEODN = %012llo %d\n", word,
                    cty_done);
       if ((word & SMASK) != 0) {
           if (cty_done) {
               word = 64LL;
               cty_done--;
#if (NUM_DEVS_TTY > 0)
           } else {
               for (ln = 0; ln < tty_desc.lines; ln++) {
                    if (tty_done[ln]) {
                        word = (((uint64)ln + 1) << 18);
                        word |=(tty_connect[ln])? 64: 1;
                        tty_done[ln] = 0;
                        break;
                    }
               }
#endif
           }
           if ((word & SMASK) == 0) {
               M[ITS_DTEODN] = word;
               /* Tell 10 something is ready */
               uptr->STATUS |= DTE_10DB;
               set_interrupt(DTE_DEVNUM, uptr->STATUS);
               sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEODN = %012llo\n",
                            word);
           }
       }
       /* Check if ready for any input */
       word = M[ITS_DTETYI];
       if ((word & SMASK) != 0) {
           /* CTY first. */
           if (not_empty(&cty_in)) {
               ch = cty_in.buff[cty_in.out_ptr];
               inco(&cty_in);
               word = (uint64)ch;
#if (NUM_DEVS_TTY > 0)
           } else {
               ln = uptr->CNT;
               while ((word & SMASK) != 0) {
                   if (not_empty(&tty_in[ln])) {
                       ch = tty_in[ln].buff[tty_in[ln].out_ptr];
                       inco(&tty_in[ln]);
                       word = ((uint64)(ln+1) << 18) | (uint64)ch;
                   }
                   ln++;
                   if (ln >= tty_desc.lines)
                      ln = 0;
                   if (ln == uptr->CNT)
                      break;
               }
               uptr->CNT = ln;
#endif
           }
           if ((word & SMASK) == 0) {
               M[ITS_DTETYI] = word;
               /* Tell 10 something is ready */
               uptr->STATUS |= DTE_10DB;
               set_interrupt(DTE_DEVNUM, uptr->STATUS);
               sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTETYI = %012llo\n",
                           word);
           }
       }
#if (NUM_DEVS_TTY > 0)
       /* Check ready for hang up message */
       word = M[ITS_DTEHNG];
       if ((word & SMASK) != 0) {
           for (ln = 0; ln < tty_desc.lines; ln++) {
                if (tty_connect[ln] != tty_ldsc[ln].conn) {
                    if (tty_ldsc[ln].conn)
                        word = 015500 + ln + 1;
                    else
                        word = ln + 1;
                    tty_connect[ln] = tty_ldsc[ln].conn;
                    tty_done[ln] = tty_ldsc[ln].conn;
                    break;
                }
           }
           /* Tell 10 something is ready */
           if ((word & SMASK) == 0) {
               M[ITS_DTEHNG] = word;
               uptr->STATUS |= DTE_10DB;
               set_interrupt(DTE_DEVNUM, uptr->STATUS);
               sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS DTEHNG = %012llo\n",
                            word);
           }
       }
#endif
    } else
#endif
    if ((uptr->STATUS & DTE_SEC) == 0) {
       /* Check if CTY done with input */
       if (cty_done) {
           data1 = PRI_CTYDV;
           if (dte_queue(PRI_EMLBE, PRI_EMDLS, 1, &data1) == 0)
               return;
           cty_done--;
       }
       /* Grab a chunck of input from CTY if any */
       n = 0;
       save_ptr = cty_in.out_ptr;
       while (not_empty(&cty_in) && n < 32) {
           ch = cty_in.buff[cty_in.out_ptr];
           inco(&cty_in);
           sim_debug(DEBUG_DETAIL, &dte_dev, "CTY recieve %02x\n", ch);
           dataq[n++] = (PRI_CTYDV << 8) | ch;
       }
       if (n > 0 && dte_queue(PRI_EMLNC, PRI_EMDLS, n, dataq) == 0) {
           /* Restore the input pointer */
           cty_in.out_ptr = save_ptr;
           return;
       }
#if (NUM_DEVS_TTY > 0)
       n = 0;
       /* While we have room for one more packet,
        * grab as much input as we can */
       for (ln = 0; ln < tty_desc.lines &&
               ((dte_out_res + 1) & 0x1f) != dte_out_ptr; ln++) {
           struct _buffer *itty = &tty_in[ln];
           while (not_empty(itty)) {
              ch = itty->buff[itty->out_ptr];
              inco(itty);
              dataq[n++] = ((ln + NUM_DLS) << 8) | ch;
              if (n == 32) {
                 if (dte_queue(PRI_EMLNC, PRI_EMDLS, n, dataq) == 0)
                     return;
                 n = 0;
                 continue;
              }
           }
       }
       if (n > 0 && dte_queue(PRI_EMLNC, PRI_EMDLS, n, dataq) == 0)
           return;
       n = 0;
       for (ln = 0; ln < tty_desc.lines; ln++) {
            data1 = (ln + NUM_DLS) | (PRI_EMDLS << 8);
            if (tty_connect[ln] != tty_ldsc[ln].conn) {
                if (tty_ldsc[ln].conn)
                    n = PRI_EMDSC;
                else
                    n = PRI_EMHUD;
                if (dte_queue(n, PRI_EMDLS, 1, &data1) == 0)
                    return;
                tty_connect[ln] = tty_ldsc[ln].conn;
            }
            if (tty_done[ln]) {
                if (dte_queue(PRI_EMLBE, PRI_EMDLS, 1, &data1) == 0)
                    return;
                tty_done[ln] = 0;
            }
       }
#endif
    }
}

/*
 * Queue up a packet to send to 10.
 */
int
dte_queue(int func, int dev, int dcnt, uint16 *data)
{
    uint16   *dp;
    struct   _dte_queue *out;

    /* Check if room in queue for this packet. */
    if (((dte_out_res + 1) & 0x1f) == dte_out_ptr) {
        sim_debug(DEBUG_DATA, &dte_dev, "DTE: %d %d out full\n", dte_out_res, dte_out_ptr);
        return 0;
    }
    out = &dte_out[dte_out_res];
    out->cnt = 10;
    out->func = func;
    out->dev = dev;
    out->dcnt = (dcnt-1)*2;
    out->spare = 0;
    sim_debug(DEBUG_DATA, &dte_dev, "DTE: %d %d queue resp: %o (%o) f=%o %s d=%o\n",
                    dte_out_ptr, dte_out_res, out->cnt, out->dcnt, out->func,
                    (out->func > PRI_EMLDV)? "***":pri_name[out->func], out->dev);
    for (dp = &out->data[0]; dcnt > 0; dcnt--) {
         *dp++ = *data++;
    }
   /* Advance pointer to next function */
   dte_out_res = (dte_out_res + 1) & 0x1f;
   return 1;
}


/*
 * If anything in queue, start a transfer, if one is not already
 * pending.
 */
int
dte_start(UNIT *uptr)
{
    uint64   word;
    int      dcnt;

    /* Check if queue empty */
    if (dte_out_ptr == dte_out_res)
        return 1;

    /* If there is interrupt pending, just return */
    if ((uptr->STATUS & (DTE_IND|DTE_10DB|DTE_11DB)) != 0)
        return 1;
    if (Mem_examine_word(0, dte_et11_off + PRI_CMTW_STS, &word)) {
error:
         /* If we can't read it, go back to secondary */
         uptr->STATUS |= DTE_SEC|DTE_10ER;
         set_interrupt(DTE_DEVNUM, uptr->STATUS);
         return 0;
    }
    /* Bump count of messages sent */
    word = (word & ~(PRI_CMT_10IC|PRI_CMT_IP)) | ((word + 0400) & PRI_CMT_10IC);
    word &= ~PRI_CMT_FWD;
    if ((uptr->STATUS & DTE_SIND) != 0)
        word |= PRI_CMT_IP;
    if (Mem_deposit_word(0, dte_dt10_off + PRI_CMTW_STS, &word))
        goto error;
    dcnt = dte_out[dte_out_ptr].cnt;
    if ((dte_out[dte_out_ptr].func & PRI_IND_FLG) == 0)
        dcnt += dte_out[dte_out_ptr].dcnt;
    /* Tell 10 something is ready */
    if ((uptr->STATUS & DTE_SIND) != 0) {
        dcnt = dte_out[dte_out_ptr].dcnt;
    }
    sim_debug(DEBUG_DATA, &dte_dev, "DTE: start: %012llo %o\n", word, dcnt);
    word = (uint64)dcnt;
    if (Mem_deposit_word(0, dte_dt10_off + PRI_CMTW_CNT, &word))
        goto error;
    uptr->STATUS |= DTE_10DB;
    set_interrupt(DTE_DEVNUM, uptr->STATUS);
    return 1;
}


/* Check for input from CTY and put on queue. */
t_stat dtei_svc (UNIT *uptr)
{
    int32    ch;
    uint32   base = 0;
    UNIT     *optr = &dte_unit[0];

#if KI_22BIT
#if KL_ITS
    if (!QITS)
#endif
    base = eb_ptr;
#endif
    sim_clock_coschedule (uptr, tmxr_poll);
    dte_input();
    if ((optr->STATUS & (DTE_SEC)) == 0) {
        dte_function(uptr);  /* Process queue */
        dte_start(optr);
    }


    /* If we have room see if any new lines */
    while (!full(&cty_in)) {
        ch = sim_poll_kbd ();
        if (ch & SCPE_KFLAG) {
            ch = 0177 & sim_tt_inpcvt(ch, TT_GET_MODE (uptr->flags));
            cty_in.buff[cty_in.in_ptr] =ch & 0377;
            inci(&cty_in);
            sim_debug(DEBUG_DETAIL, &dte_dev, "CTY char %o '%c'\n", ch,
                            ((ch > 040 && ch < 0177)? ch: '.'));
        } else
            break;
    }

    /* If Monitor input, place in buffer */
    if ((optr->STATUS & (DTE_SEC|DTE_MON)) == (DTE_SEC|DTE_MON) &&
         not_empty(&cty_in) && M[SEC_DTMTI + base] == 0) {
        ch = cty_in.buff[cty_in.out_ptr];
        inco(&cty_in);
        M[SEC_DTF11 + base] = ch;
        M[SEC_DTMTI + base] = FMASK;
        if (dte_dev.flags & TYPE_RSX20) {
            uptr->STATUS |= DTE_10DB;
            set_interrupt(DTE_DEVNUM, dte_unit[0].STATUS);
        }
    }
    return SCPE_OK;
}

/* Handle output of characters to CTY. Started whenever there is output pending */
t_stat dteo_svc (UNIT *uptr)
{
    /* Flush out any pending CTY output */
    while(not_empty(&cty_out)) {
        char ch = cty_out.buff[cty_out.out_ptr];
        if (ch != 0) {
            if (sim_putchar_s(ch) != SCPE_OK) {
                sim_activate(uptr, 1000);
                return SCPE_OK;;
            }
        }
        inco(&cty_out);
        sim_debug(DEBUG_DETAIL, &dte_dev, "CTY outch %o '%c'\n", ch,
                            ((ch > 040 && ch < 0177)? ch: '.'));
    }
    cty_done++;
    return SCPE_OK;
}


/* Handle FE timer interrupts. And keepalive counts */
t_stat
dtertc_srv(UNIT * uptr)
{
    UNIT     *optr = &dte_unit[0];

    sim_activate_after(uptr, 1000000/rtc_tps);
    /* Check if clock requested */
    if (uptr->STATUS & SEC_CLK) {
        rtc_tick++;
        if (rtc_wait != 0) {
            rtc_wait--;
        } else {
            UNIT     *optr = &dte_unit[0];
            uint32   base = 0;
#if KI_22BIT
            base = eb_ptr;
#endif
            /* Set timer flag */
            M[SEC_DTCLK + base] = FMASK;
            optr->STATUS |= DTE_10DB;
            set_interrupt(DTE_DEVNUM, optr->STATUS);
            sim_debug(DEBUG_EXP, &dte_dev, "CTY tick %x %x %06o\n",
                          rtc_tick, rtc_wait, optr->STATUS);
        }
    }
#if KL_ITS
    /* Check if Timesharing is running */
    if (QITS) {
        uint64     word;

        word = (M[ITS_DTECHK] + 1) & FMASK;
        if (word == 0) {
            optr->STATUS |= ITS_ON;
            sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS ON\n");
            sim_activate(&tty_unit[0], 1000);
            sim_activate(&tty_unit[1], 1000);
        } else if (word >= (15 * 60)) {
            optr->STATUS &= ~ITS_ON;
            word = 15 * 60;
            sim_cancel(&tty_unit[0]);
            sim_cancel(&tty_unit[1]);
            sim_debug(DEBUG_DETAIL, &dte_dev, "CTY ITS OFF\n");
        }
        M[ITS_DTECHK] = word;
    } else
#endif

    /* Update out keep alive timer if in secondary protocol */
    if ((optr->STATUS & DTE_SEC) == 0) {
        int      addr = 0144 + eb_ptr;
        uint64   word;

        (void)Mem_examine_word(0, dte_et11_off + PRI_CMTW_STS, &word);
        addr = (M[addr+1] + dte_off + PRI_CMTW_KAC) & RMASK;
        word = M[addr];
        word = (word + 1) & FMASK;
        M[addr] = word;
      sim_debug(DEBUG_EXP, &dte_dev, "CTY keepalive %06o %012llo %06o\n",
                          addr, word, optr->STATUS);
    }

    return SCPE_OK;
}


t_stat dte_reset (DEVICE *dptr)
{
    dte_unit[0].STATUS = DTE_SEC;
    dte_unit[1].STATUS = 0;
    dte_unit[2].STATUS = 0;
    dte_unit[3].STATUS = 0;
    cty_done = 0;
    sim_activate(&dte_unit[3], 1000);
    sim_activate(&dte_unit[2], 1000);
    return SCPE_OK;
}


t_stat
dte_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
       return SCPE_IERR;
    dptr->flags &= ~DEV_M_OS;
    dptr->flags |= val;
    return SCPE_OK;
}

t_stat
dte_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   DEVICE *dptr;

   if (uptr == NULL)
      return SCPE_IERR;

   dptr = find_dev_from_unit(uptr);
   if (dptr == NULL)
      return SCPE_IERR;
   fprintf (st, "%s", (dptr->flags & TYPE_RSX20) ? "RSX20" : "RSX10");
   return SCPE_OK;
}


/* Stop operating system */

t_stat dte_stop_os (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    M[CTY_SWITCH] = 1;                                 /* tell OS to stop */
    return SCPE_OK;
}

t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    dte_unit[0].flags = (dte_unit[0].flags & ~TT_MODE) | val;
    return SCPE_OK;
}

t_stat dte_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "To stop the cpu use the command:\n\n");
fprintf (st, "    sim> SET CTY STOP\n\n");
fprintf (st, "This will write a 1 to location %03o, causing TOPS10 to stop\n\n", CTY_SWITCH);
fprintf (st, "The additional terminals can be set to one of four modes: UC, 7P, 7B, or 8B.\n\n");
fprintf (st, "  mode  input characters        output characters\n\n");
fprintf (st, "  UC    lower case converted    lower case converted to upper case,\n");
fprintf (st, "        to upper case,          high-order bit cleared,\n");
fprintf (st, "        high-order bit cleared  non-printing characters suppressed\n");
fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                non-printing characters suppressed\n");
fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B    no changes              no changes\n\n");
fprintf (st, "The default mode is 7P.  In addition, each line can be configured to\n");
fprintf (st, "behave as though it was attached to a dataset, or hardwired to a terminal:\n\n");
fprint_reg_help (st, &dte_dev);
return SCPE_OK;
}

const char *dte_description (DEVICE *dptr)
{
    return "Console TTY Line";
}


#if (NUM_DEVS_LP20 > 0)

void
lp20_printline(UNIT *uptr, int nl) {
    int     trim = 0;

    /* Trim off trailing blanks */
    while (uptr->COL >= 0 && lp20_buffer[uptr->COL - 1] == ' ') {
         uptr->COL--;
         trim = 1;
    }
    lp20_buffer[uptr->COL] = '\0';
    sim_debug(DEBUG_DETAIL, &lp20_dev, "LP output %d %d [%s]\n", uptr->COL, nl,
              lp20_buffer);
    /* Stick a carraige return and linefeed as needed */
    if (uptr->COL != 0 || trim)
        lp20_buffer[uptr->COL++] = '\r';
    if (nl != 0) {
        lp20_buffer[uptr->COL++] = '\n';
        uptr->LINE++;
    }
    if (nl > 0 && lp20_vfu[uptr->LINE] == 010000) {
        lp20_buffer[uptr->COL++] = '\f';
        uptr->LINE = 1;
    } else if (nl < 0 && uptr->LINE >= (int32)uptr->capac) {
        uptr->LINE = 1;
    }

    sim_fwrite(&lp20_buffer, 1, uptr->COL, uptr->fileref);
    uptr->pos += uptr->COL;
    uptr->COL = 0;
    return;
}


/* Unit service */
void
lp20_output(UNIT *uptr, char c) {

    if (c == 0)
       return;
    if (uptr->COL == 132)
        lp20_printline(uptr, 1);
    if ((uptr->flags & UNIT_UC) && (c & 0140) == 0140)
        c &= 0137;
    else if (c >= 040 && c < 0177) { /* If printable */
        lp20_buffer[uptr->COL++] = c;
    } if (c == 011) { /* Tab */
        lp20_buffer[uptr->COL++] = ' ';
        while ((uptr->COL & 07) != 0)
            lp20_buffer[uptr->COL++] = ' ';
    }
    return;
}

t_stat lp20_svc (UNIT *uptr)
{
    char    ch;
    uint16  ram_ch;
    uint16  data1[5];

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_OK;
    if (dte_dev.flags & TYPE_RSX20 && uptr->LPST & HDSFLG) {
        data1[0] = 0;

        data1[1] = (uptr->LINE == 1) ? 01<<8: 0;
        sim_debug(DEBUG_DETAIL, &dte_dev, "LPT status %06o \n", uptr->LPST);
        if (uptr->LPST & EOFFLG) {
            data1[0] |= 040 << 8;
            uptr->LPCNT = 0;
        }
        if (uptr->LPST & INTFLG) {
            data1[1] |= 02 << 8;
            uptr->LPCNT = 0;
        }
        data1[2] = 0110200; 
        if (dte_queue(PRI_EMHDS+PRI_IND_FLG, PRI_EMLPT, 4, data1) == 0)
            sim_activate(uptr, 1000);
        uptr->LPST &= ~(HDSFLG);
    }

    if (empty(&lp20_queue))
           return SCPE_OK;
    while (not_empty(&lp20_queue)) {
        ch = lp20_queue.buff[lp20_queue.out_ptr];
        inco(&lp20_queue);
        ram_ch = lp20_ram[(int)ch];

        /* If previous was delimiter or translation do it */
        if (uptr->LPST & DELFLG || (ram_ch &(LP20_RAM_DEL|LP20_RAM_TRN)) != 0) {
            ch = ram_ch & LP20_RAM_CHR;
            uptr->LPST &= ~DELFLG;
            if (ram_ch & LP20_RAM_DEL)
               uptr->LPST |= DELFLG;
        }
        /* Flag if interrupt set */
        if (ram_ch & LP20_RAM_INT)
            uptr->LPST |= HDSFLG|INTFLG;
        /* Check if paper motion */
        if (ram_ch & LP20_RAM_PI) {
            int   lines = 0;  /* Number of new lines to output */
            /* Print any buffered line */
            lp20_printline(uptr, (ram_ch & 037) != 020);
            sim_debug(DEBUG_DETAIL, &lp20_dev, "LP deque %02x %04x\n",
                                 ch, ram_ch);
            if ((ram_ch & 020) == 0) { /* Find channel mark in output */
               while ((lp20_vfu[uptr->LINE] & (1 << (ram_ch & 017))) == 0) {
                   sim_debug(DEBUG_DETAIL, &lp20_dev,
                                 "LP skip chan %04x %04x %d\n",
                                 lp20_vfu[uptr->LINE], ram_ch, uptr->LINE);
                   if (lp20_vfu[uptr->LINE] & 010000) { /* Hit bottom of form */
                      sim_fwrite("\014", 1, 1, uptr->fileref);
                      uptr->pos++;
                      lines = 0;
                      uptr->LINE = 1;
                      break;
                   }
                   lines++;
                   uptr->LINE++;
               }
            } else {
               while ((ram_ch & 017) != 0) {
                   sim_debug(DEBUG_DETAIL, &lp20_dev,
                                "LP skip line %04x %04x %d\n",
                                 lp20_vfu[uptr->LINE], ram_ch, uptr->LINE);
                   if (lp20_vfu[uptr->LINE] & 010000) { /* Hit bottom of form */
                      sim_fwrite("\014", 1, 1, uptr->fileref);
                      uptr->pos++;
                      lines = 0;
                      uptr->LINE = 1;
                   }
                   lines++;
                   uptr->LINE++;
                   ram_ch--;
               }
            }
            for(;lines > 0; lines--) {
               sim_fwrite("\r\n", 1, 2, uptr->fileref);
               uptr->pos+=2;
            }
        } else if (ch != 0) {
            sim_debug(DEBUG_DETAIL, &lp20_dev, "LP deque %02x '%c' %04x\n",
                                  ch, ch, ram_ch);
            lp20_output(uptr, ch);
        }
    }
    if (empty(&lp20_queue)) {
        data1[0] = 0;
        if (dte_queue(PRI_EMLBE, PRI_EMLPT, 1, data1) == 0)
           sim_activate(uptr, 1000);
        if (dte_dev.flags & TYPE_RSX20) {
            if (uptr->LINE == 0) {
                uptr->LPST |= HDSFLG;
               sim_activate(uptr, 1000);
            }
        }
    }
    return SCPE_OK;
}

/* Reset routine */

t_stat lp20_reset (DEVICE *dptr)
{
    UNIT *uptr = &lp20_unit;
    int   i;
    uptr->POS = 0;
    uptr->COL = 0;
    uptr->LINE = 1;
    /* Clear RAM & VFU */
    for (i = 0; i < 256; i++) {
       lp20_ram[i] = 0;
       lp20_vfu[i] = 0;
    }

    /* Load default VFU into VFU */
    memcpy(&lp20_vfu, lp20_dvfu, sizeof(lp20_dvfu));
    lp20_ram[012] = LP20_RAM_TRN|LP20_RAM_PI|7;   /* Line feed, print line, space one line */
    lp20_ram[013] = LP20_RAM_TRN|LP20_RAM_PI|6;   /* Vertical tab, Skip mod 20 */
    lp20_ram[014] = LP20_RAM_TRN|LP20_RAM_PI|0;   /* Form feed, skip to top of page */
    lp20_ram[015] = LP20_RAM_TRN|LP20_RAM_PI|020; /* Carrage return */
    lp20_ram[020] = LP20_RAM_TRN|LP20_RAM_PI|1;   /* Skip half page */
    lp20_ram[021] = LP20_RAM_TRN|LP20_RAM_PI|2;   /* Skip even lines */
    lp20_ram[022] = LP20_RAM_TRN|LP20_RAM_PI|3;   /* Skip triple lines */
    lp20_ram[023] = LP20_RAM_TRN|LP20_RAM_PI|4;   /* Skip one line */
    lp20_ram[024] = LP20_RAM_TRN|LP20_RAM_PI|5;
    sim_cancel (&lp20_unit);                                 /* deactivate unit */
    return SCPE_OK;
}

/* Attach routine */

t_stat lp20_attach (UNIT *uptr, CONST char *cptr)
{
    sim_switches |= SWMASK ('A');   /* Position to EOF */
    return attach_unit (uptr, cptr);
}

/* Detach routine */

t_stat lp20_detach (UNIT *uptr)
{
    return detach_unit (uptr);
}

/*
 * Line printer routines
 */

t_stat
lp20_setlpp(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_value   i;
    t_stat    r;
    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    i = get_uint (cptr, 10, 100, &r);
    if (r != SCPE_OK)
        return SCPE_ARG;
    uptr->capac = (t_addr)i;
    uptr->LINE = 0;
    return SCPE_OK;
}

t_stat
lp20_getlpp(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "linesperpage=%d", uptr->capac);
    return SCPE_OK;
}

t_stat lp20_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr)
{
fprintf (st, "Line Printer (LPT)\n\n");
fprintf (st, "The line printer (LPT) writes data to a disk file.  The POS register specifies\n");
fprintf (st, "the number of the next data item to be written.  Thus, by changing POS, the\n");
fprintf (st, "user can backspace or advance the printer.\n");
fprintf (st, "The Line printer can be configured to any number of lines per page with the:\n");
fprintf (st, "        sim> SET %s0 LINESPERPAGE=n\n\n", dptr->name);
fprintf (st, "The default is 66 lines per page.\n\n");
fprintf (st, "The device address of the Line printer can be changed\n");
fprintf (st, "        sim> SET %s0 DEV=n\n\n", dptr->name);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *lp20_description (DEVICE *dptr)
{
    return "LP20 line printer" ;
}

#endif

#if (NUM_DEVS_TTY > 0)

/* Unit service */
t_stat ttyi_svc (UNIT *uptr)
{
    int32    ln;
    TMLN     *lp;
    int      flg;

    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;

    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */

    /* If we have room see if any new lines */
    ln = tmxr_poll_conn (&tty_desc);                    /* look for connect */
    if (ln >= 0) {
        tty_ldsc[ln].rcve = 1;
            sim_debug(DEBUG_DETAIL, &tty_dev, "TTY line connect %d\n", ln);
    }

    tmxr_poll_tx(&tty_desc);
    tmxr_poll_rx(&tty_desc);

    /* Scan each line for input */
    for (ln = 0; ln < tty_desc.lines; ln++) {
        struct _buffer  *iptr = &tty_in[ln];
        lp = &tty_ldsc[ln];
        if (lp->conn == 0)
           continue;
        flg = 1;
        while (flg && !full(iptr)) {
        /* Spool up as much as we have room for */
            int32 ch = tmxr_getc_ln(lp);
            if ((ch & TMXR_VALID) != 0) {
                ch = sim_tt_inpcvt (ch,
                                    TT_GET_MODE(tty_unit[0].flags) | TTUF_KSR);
                iptr->buff[iptr->in_ptr] = ch & 0377;
                inci(iptr);
                sim_debug(DEBUG_DETAIL, &tty_dev, "TTY recieve %d: %o\n",
                               ln, ch);
            } else
                flg = 0;
        }
    }

    return SCPE_OK;
}

/* Output whatever we can */
t_stat ttyo_svc (UNIT *uptr)
{
    t_stat   r;
    int32    ln;
    TMLN     *lp;

    if ((tty_unit[0].flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;

    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */

    for (ln = 0; ln < tty_desc.lines; ln++) {
       struct _buffer  *optr = &tty_out[ln];
       lp = &tty_ldsc[ln];
       if (lp->conn == 0) {
           if (not_empty(optr)) {
               optr->out_ptr = optr->in_ptr = 0;
               tty_done[ln] = 1;
           }
           continue;
       }
       if (empty(optr))
           continue;
       while (not_empty(optr)) {
           int32 ch = optr->buff[optr->out_ptr];
           ch = sim_tt_outcvt(ch, TT_GET_MODE (tty_unit[0].flags) | TTUF_KSR);
           sim_debug(DEBUG_DATA, &tty_dev, "TTY: %d output %o\n", ln, ch);
           r = tmxr_putc_ln (lp, ch);
           if (r == SCPE_OK)
               inco(optr);
           else if (r == SCPE_LOST) {
               optr->out_ptr = optr->in_ptr = 0;
               continue;
           } else
               continue;
       }
       tty_done[ln] = 1;
    }
    return SCPE_OK;
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
    return SCPE_OK;
}


/* SET LINES processor */

t_stat tty_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 newln, i, t;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, NUM_LINES_TTY, &r);
    if ((r != SCPE_OK) || (newln == tty_desc.lines))
        return r;
    if ((newln == 0) || (newln >= NUM_LINES_TTY) || (newln % 16) != 0)
        return SCPE_ARG;
    if (newln < tty_desc.lines) {
        for (i = newln, t = 0; i < tty_desc.lines; i++)
            t = t | tty_ldsc[i].conn;
        if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
        for (i = newln; i < tty_desc.lines; i++) {
            if (tty_ldsc[i].conn) {
                tmxr_linemsg (&tty_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data (&tty_ldsc[i]);
                }
            tmxr_detach_ln (&tty_ldsc[i]);        /* completely reset line */
        }
    }
    if (tty_desc.lines < newln)
        memset (tty_ldsc + tty_desc.lines, 0,
                 sizeof(*tty_ldsc)*(newln-tty_desc.lines));
    tty_desc.lines = newln;
    return tty_reset (&tty_dev);              /* setup lines and auto config */
}

/* SET LOG processor */

t_stat tty_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    char gbuf[CBUFSIZE];
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    cptr = get_glyph (cptr, gbuf, '=');
    if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
        return SCPE_ARG;
    ln = (int32) get_uint (gbuf, 10, tty_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= tty_desc.lines))
        return SCPE_ARG;
    return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

t_stat tty_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, tty_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= tty_desc.lines))
        return SCPE_ARG;
    return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat tty_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32 i;

    for (i = 0; i < tty_desc.lines; i++) {
        fprintf (st, "line %d: ", i);
        tmxr_show_log (st, NULL, i, desc);
        fprintf (st, "\n");
        }
    return SCPE_OK;
}


/* Attach routine */

t_stat tty_attach (UNIT *uptr, CONST char *cptr)
{
t_stat reason;

reason = tmxr_attach (&tty_desc, uptr, cptr);
if (reason != SCPE_OK)
  return reason;
sim_activate (uptr, tmxr_poll);
return SCPE_OK;
}

/* Detach routine */

t_stat tty_detach (UNIT *uptr)
{
  int32  i;
  t_stat reason;
sim_cancel (uptr);
reason = tmxr_detach (&tty_desc, uptr);
for (i = 0; i < tty_desc.lines; i++)
    tty_ldsc[i].rcve = 0;
return reason;
}

t_stat tty_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "FE Terminal Interfaces\n\n");
fprintf (st, "The FE terminal could support up to 256 lines, in groups of 16\n");
fprintf (st, "lines. The number of lines is specified with a SET command:\n\n");
fprintf (st, "   sim> SET TTY LINES=n          set number of additional lines to n [8-32]\n\n");
fprintf (st, "Lines must be set in multiples of 8.\n");
fprintf (st, "The ATTACH command specifies the port to be used:\n\n");
tmxr_attach_help (st, dptr, uptr, flag, cptr);
fprintf (st, "The additional terminals can be set to one of four modes: UC, 7P, 7B, or 8B.\n\n");
fprintf (st, "  mode  input characters        output characters\n\n");
fprintf (st, "  UC    lower case converted    lower case converted to upper case,\n");
fprintf (st, "        to upper case,          high-order bit cleared,\n");
fprintf (st, "        high-order bit cleared  non-printing characters suppressed\n");
fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                non-printing characters suppressed\n");
fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B    no changes              no changes\n\n");
fprintf (st, "The default mode is 7P.\n");
fprintf (st, "Finally, each line supports output logging.  The SET TTYn LOG command enables\n");
fprintf (st, "logging on a line:\n\n");
fprintf (st, "   sim> SET TTYn LOG=filename   log output of line n to filename\n\n");
fprintf (st, "The SET TTYn NOLOG command disables logging and closes the open log file,\n");
fprintf (st, "if any.\n\n");
fprintf (st, "Once TTY is attached and the simulator is running, the terminals listen for\n");
fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
fprintf (st, "by the Telnet client, a SET TTY DISCONNECT command, or a DETACH TTY command.\n\n");
fprintf (st, "Other special commands:\n\n");
fprintf (st, "   sim> SHOW TTY CONNECTIONS    show current connections\n");
fprintf (st, "   sim> SHOW TTY STATISTICS     show statistics for active connections\n");
fprintf (st, "   sim> SET TTYn DISCONNECT     disconnects the specified line.\n");
fprint_reg_help (st, &tty_dev);
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or TTY is detached.\n");
return SCPE_OK;
}

const char *tty_description (DEVICE *dptr)
{
return "FE asynchronous line interface";
}

#endif
#endif
