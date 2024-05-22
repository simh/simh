/* kl10_dn.c: KL-10 DN network interface.

   Copyright (c) 2019-2021, Richard Cornwell

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

#ifndef NUM_DEVS_DN
#define NUM_DEVS_DN 0
#endif

#if KL
#if (NUM_DEVS_DN > 0)
#define UNIT_DUMMY      (1 << UNIT_V_UF)

#define DTE_DEVNUM       0204
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
#define DTE_INIT       040000000   /* Recieved an INIT signal last time */

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
#define PRI_NCL        011          /* NCL device */
#define PRI_DN60       012          /* DN60 */
#define PRI_CTYDV      000          /* Line number for CTY */
#define NUM_DLS        5            /* Number of first DH Line */


extern int32 tmxr_poll;
t_stat dn_devio(uint32 dev, uint64 *data);
t_addr dn_devirq(uint32 dev, t_addr addr);
void   dn_second(UNIT *uptr);
void   dn_primary(UNIT *uptr);
void   dn_transfer(UNIT *uptr);
void   dn_function(UNIT *uptr);
void   dn_input();
int    dn_start(UNIT *uptr);
int    dn_queue(int func, int dev, int dcnt, uint16 *data);
t_stat dni_svc (UNIT *uptr);
t_stat dn_svc (UNIT *uptr);
t_stat dno_svc (UNIT *uptr);
t_stat dnrtc_srv(UNIT * uptr);
t_stat dn_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dn_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dn_reset (DEVICE *dptr);
t_stat dn_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *dn_description (DEVICE *dptr);
extern uint64  SW;                                   /* Switch register */

extern CONST char *pri_name[];

#define STATUS            u3
#define CNT               u4

extern uint32  eb_ptr;

struct _dn_queue {
    int         dptr;      /* Pointer to working item */
    uint16      cnt;       /* Number of bytes in packet */
    uint16      func;      /* Function code */
    uint16      dev;       /* Dev code */
    uint16      spare;     /* Dev code */
    uint16      dcnt;      /* Data count */
    uint16      data[258]; /* Data packet */
    uint16      sdev;      /* Secondary device code */
    uint16      sz;        /* Byte size */
} dn_in[32], dn_out[32];

int32 dn_in_ptr;
int32 dn_in_cmd;
int32 dn_out_ptr;
int32 dn_out_res;
int32 dn_base;            /* Base */
int32 dn_off;             /* Our offset */
int32 dn_dt10_off;        /* Offset to 10 deposit region */
int32 dn_et10_off;        /* Offset to 10 examine region */
int32 dn_et11_off;        /* Offset to 11 examine region */
int32 dn_proc_num;        /* Our processor number */

struct _buffer {
    int      in_ptr;     /* Insert pointer */
    int      out_ptr;    /* Remove pointer */
    char     buff[256];  /* Buffer */
};

#define full(q)       ((((q)->in_ptr + 1) & 0xff) == (q)->out_ptr)
#define empty(q)      ((q)->in_ptr == (q)->out_ptr)
#define not_empty(q)  ((q)->in_ptr != (q)->out_ptr)
#define inco(q)       (q)->out_ptr = ((q)->out_ptr + 1) & 0xff
#define inci(q)       (q)->in_ptr = ((q)->in_ptr + 1) & 0xff

DIB dn_dib[] = {
    { DTE_DEVNUM|000, 1, dn_devio, dn_devirq},
};

MTAB dn_mod[] = {
    {MTAB_XTD|MTAB_VDV, TYPE_RSX10, NULL, "RSX10",  &dn_set_type, NULL,
              NULL, "Sets DTE to RSX10 mode"},
    {MTAB_XTD|MTAB_VDV, TYPE_RSX20, "RSX20", "RSX20", &dn_set_type, &dn_show_type,
              NULL, "Sets DTE to RSX20 mode"},
    { 0 }
    };

UNIT dn_unit[] = {
    { UDATA (&dn_svc, TT_MODE_7B, 0), 100},
    { UDATA (&dno_svc, TT_MODE_7B, 0), 100},
    { UDATA (&dni_svc, TT_MODE_7B|UNIT_DIS, 0), 1000 },
    };

REG  dn_reg[] = {
    {SAVEDATA(IN, dn_in) },
    {SAVEDATA(OUT, dn_out) },
    {HRDATA(IN_PTR, dn_in_ptr, 32), REG_HRO},
    {HRDATA(IN_CMD, dn_in_cmd, 32), REG_HRO},
    {HRDATA(OUT_PTR, dn_out_ptr, 32), REG_HRO},
    {HRDATA(OUT_RES, dn_out_res, 32), REG_HRO},
    {HRDATA(BASE, dn_base, 32), REG_HRO},
    {HRDATA(OFF, dn_off, 32), REG_HRO},
    {HRDATA(DTOFF, dn_dt10_off, 32), REG_HRO},
    {HRDATA(ETOFF, dn_et10_off, 32), REG_HRO},
    {HRDATA(E1OFF, dn_et11_off, 32), REG_HRO},
    {HRDATA(PROC, dn_proc_num, 32), REG_HRO},
    {HRDATAD(WRU, sim_int_char, 8, "interrupt character") },
    { 0 },
    };


DEVICE dn_dev = {
    "DN", dn_unit, dn_reg, dn_mod,
    3, 10, 31, 1, 8, 8,
    NULL, NULL, &dn_reset,
    NULL, NULL, NULL, &dn_dib, DEV_DIS | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dn_help, NULL, NULL, &dn_description
    };


t_stat dn_devio(uint32 dev, uint64 *data) {
     uint32     res;
     switch(dev & 3) {
     case CONI:
        *data = (uint64)(dn_unit[0].STATUS) & RMASK;
        *data |= DTE_RM;    /* Restricted mode */
        sim_debug(DEBUG_CONI, &dn_dev, "DN %03o CONI %06o\n", dev, (uint32)*data);
        break;
     case CONO:
         res = (uint32)(*data & RMASK);
         clr_interrupt(dev);
         if (res & DTE_PIENB) {
             dn_unit[0].STATUS &= ~(DTE_PIA|DTE_PIE);
             dn_unit[0].STATUS |= res & (DTE_PIA|DTE_PIE);
         }
         if (res & DTE_CO11CL)
             dn_unit[0].STATUS &= ~(DTE_11DN|DTE_11ER);
         if (res & DTE_CO10CL) {
             dn_unit[0].STATUS &= ~(DTE_10DN|DTE_10ER);
             dn_start(&dn_unit[0]);
         }
         if (res & DTE_CO10DB)
             dn_unit[0].STATUS &= ~(DTE_10DB);
         if (res & DTE_CO11CR)
             dn_unit[0].STATUS &= ~(DTE_11RELD);
         if (res & DTE_CO11SR)
             dn_unit[0].STATUS |= (DTE_11RELD);
         if (res & DTE_CO11DB) {
             sim_debug(DEBUG_CONO, &dn_dev, "DN Ring 11 DB\n");
             dn_unit[0].STATUS |= DTE_11DB;
             dn_unit[0].STATUS &= ~(DTE_10DB);
             sim_activate(&dn_unit[0], 200);
         }
         if (dn_unit[0].STATUS & (DTE_10DB|DTE_11DN|DTE_10DN|DTE_11ER|DTE_10ER))
             set_interrupt(dev, dn_unit[0].STATUS);
         sim_debug(DEBUG_CONO, &dn_dev, "DN %03o CONO %06o %06o\n", dev,
                      (uint32)*data, PC);
         break;
     case DATAI:
         sim_debug(DEBUG_DATAIO, &dn_dev, "DN %03o DATAI %06o\n", dev,
                      (uint32)*data);
         break;
    case DATAO:
         sim_debug(DEBUG_DATAIO, &dn_dev, "DN %03o DATAO %06o\n", dev,
                      (uint32)*data);
         if (*data == 01365) {
             dn_unit[0].STATUS |= DTE_10ER;
             dn_unit[0].STATUS &= ~(DTE_10DB|DTE_IND|DTE_11DB);
             break;
         }
         dn_unit[0].CNT = (*data & (DTE_TO10IB|DTE_TO10BC));
         dn_unit[0].STATUS |= DTE_TO11;
         sim_activate(&dn_unit[0], 10);
         break;
    }
    return SCPE_OK;
}

/* Handle KL style interrupt vectors */
t_addr
dn_devirq(uint32 dev, t_addr addr) {
    return 0152;
}

/* Handle TO11 interrupts */
t_stat dn_svc (UNIT *uptr)
{
    /* Did the 10 knock? */
    if (uptr->STATUS & DTE_11DB) {
        /* If in secondary mode, do that protocol */
        if (uptr->STATUS & DTE_SEC)
            dn_second(uptr);
        dn_primary(uptr);   /* Retrieve data */
    } else if (uptr->STATUS & DTE_TO11) {
        /* Does 10 want us to send it what we have? */
        dn_transfer(uptr);
    }
    return SCPE_OK;
}

/* Handle secondary protocol */
void dn_second(UNIT *uptr) {
    uint64   word;
    int32    ch;
    uint32   base = 0;
int i;

#if KI_22BIT
    base = eb_ptr;
#endif
    /* read command */
    word = M[SEC_DTCMD + base];
    /* Do it */
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN secondary %012llo\n", word);
for (i = 0; i < 8; i++)
   sim_debug(DEBUG_DETAIL, &dn_dev, "EB word %o %012llo\n", i, M[eb_ptr + 0150+i]);
for (i = 0; i <100; i++) {
   if (Mem_examine_word(1, i, &word))
      break;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 word %3o %012llo\n", i, word);
}
    switch(word & SEC_CMDMSK) {
    default:
#if 0
    case SEC_MONO:  /* Ouput character in monitor mode */
         ch = (int32)(word & 0177);
         if (full(&cty_out)) {
            sim_activate(uptr, 200);
            return;
         }
         if (ch != 0) {
             cty_out.buff[cty_out.in_ptr] = ch & 0x7f;
             inci(&cty_out);
             sim_activate(&dn_unit[1], 200);
         }
         M[SEC_DTCHR + base] = ch;
         M[SEC_DTMTD + base] = FMASK;
         break;
#endif

     case SEC_SETPRI:
enter_pri:
         if (Mem_examine_word(1, 0, &word))
             return;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN word 0 %012llo\n", word);
         dn_proc_num = (word >> 24) & 037;
         dn_base = dn_proc_num + 1;
         dn_off = dn_base + (word & 0177777);
         dn_dt10_off = 16;
         dn_et10_off = dn_dt10_off + 16;
         dn_et11_off = dn_base + 16;
         uptr->STATUS &= ~DTE_SEC;
         dn_in_ptr = dn_out_ptr = 0;
         dn_in_cmd = dn_out_res = 0;

         /* Start input process */
         M[SEC_DTCMD + base] = 0;
         M[SEC_DTFLG + base] = FMASK;
         uptr->STATUS &= ~DTE_11DB;
         return;
#if 0
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
         break;
#endif
     }
     /* Acknowledge command */
     M[SEC_DTCMD + base] = 0;
     M[SEC_DTFLG + base] = FMASK;
     uptr->STATUS &= ~DTE_11DB;
     if (dn_dev.flags & TYPE_RSX20) {
         uptr->STATUS |= DTE_10DB;
         set_interrupt(DTE_DEVNUM, dn_unit[0].STATUS);
     }
}


/* Handle primary protocol */
void dn_primary(UNIT *uptr) {
    uint64   word, iword;
    int      s;
    int      cnt;
    struct   _dn_queue *in;
    uint16   data1, *dp;
int i;

    if ((uptr->STATUS & DTE_11DB) == 0)
        return;

    /* Check if there is room for another packet */
    if (((dn_in_ptr + 1) & 0x1f) == dn_in_cmd) {
        /* If not reschedule ourselves */
        sim_activate(uptr, 100);
        return;
    }
    uptr->STATUS &= ~(DTE_11DB);
    clr_interrupt(DTE_DEVNUM);
         Mem_examine_word(1, 0, &word);
         dn_proc_num = (word >> 24) & 037;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 procnum %0o\n", dn_proc_num);
         dn_base = dn_proc_num + 1;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 base %0o\n", dn_base);
         dn_off = dn_base + (word & 0177777);
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 dn_off %0o\n", dn_off);
         dn_dt10_off = 020;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 dn_dt_off %0o\n", dn_dt10_off);
         dn_et10_off = dn_dt10_off + 16;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 dn_et_off %0o\n", dn_et10_off);
         dn_et11_off = dn_base + (8 * dn_base);
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 dn_et11_off %0o\n", dn_et11_off);

for (i = 0; i < 8; i++)
   sim_debug(DEBUG_DETAIL, &dn_dev, "EB word %o %012llo\n", i, M[eb_ptr + 0150+i]);
for (i = 0; i <200; i++) {
   if (Mem_examine_word(1, i, &word))
      break;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 word %3o %012llo\n", i, word);
}
    /* Check status word to see if valid */
    if (Mem_examine_word(1, dn_et11_off + PRI_CMTW_STS, &word)) {
         uint32   base;
error:
         base = 0;
#if KI_22BIT
         base = eb_ptr;
#endif
         /* If we can't read it, go back to secondary */
         M[SEC_DTFLG + base] = FMASK;
//         uptr->STATUS |= DTE_SEC;
         uptr->STATUS &= ~DTE_11DB;
         if (dn_dev.flags & TYPE_RSX20) {
             uptr->STATUS |= DTE_10DB;
             set_interrupt(DTE_DEVNUM, dn_unit[0].STATUS);
         }
         sim_debug(DEBUG_DETAIL, &dn_dev, "DTE: error %012llo\n", word);
         return;
    }

    sim_debug(DEBUG_DETAIL, &dn_dev, "DTE: status %06llo %012llo\n", dn_et11_off + PRI_CMTW_STS + M[0155 + eb_ptr], word);
    if ((word & PRI_CMT_INI) != 0) {
         word &= ~PRI_CMT_TOT;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DTE: istatus %06llo %012llo\n", dn_dt10_off + PRI_CMTW_STS + M[0157 + eb_ptr], word);
         if (Mem_deposit_word(1, dn_dt10_off + PRI_CMTW_STS, &word))
             goto error;
         uptr->STATUS |= DTE_11DN|DTE_10DB|DTE_INIT;
         set_interrupt(DTE_DEVNUM, uptr->STATUS);
         return;
    }
    if ((uptr->STATUS & DTE_INIT) != 0) {
    sim_debug(DEBUG_DETAIL, &dn_dev, "DTE: dstatus %06llo %012llo\n", dn_dt10_off + PRI_CMTW_STS + M[0157 + eb_ptr], word);
         if (Mem_deposit_word(1, dn_dt10_off + PRI_CMTW_STS, &word))
             goto error;
         uptr->STATUS |= DTE_11DN|DTE_10DB;
         uptr->STATUS &= ~DTE_INIT;
         set_interrupt(DTE_DEVNUM, uptr->STATUS);
         return;
    }
//    if ((word & PRI_CMT_QP) == 0) {
 //       goto error;
  //  }
        
    in = &dn_in[dn_in_ptr];
    /* Check if indirect */
    if ((word & PRI_CMT_IP) != 0) {
        /* Transfer from 10 */
        if ((uptr->STATUS & DTE_IND) == 0) {
            fprintf(stderr, "DTE out of sync\r\n");
            return;
        }
        /* Get size of transfer */
        if (Mem_examine_word(1, dn_et11_off + PRI_CMTW_CNT, &iword))
            goto error;
        sim_debug(DEBUG_EXP, &dn_dev, "DTE: count: %012llo\n", iword);
        in->dcnt = (uint16)(iword & 0177777);
        /* Read in data */
        dp = &in->data[0];
        for (cnt = in->dcnt; cnt > 0; cnt --) {
            /* Read in data */
            s = Mem_read_byte(1, dp, 0);
            if (s == 0)
               goto error;
            in->sz = s;
            sim_debug(DEBUG_DATA, &dn_dev,
                   "DTE: Read Idata: %06o %03o %03o %06o cnt=%o\n",
                    *dp, *dp >> 8, *dp & 0377,
                    ((*dp & 0377) << 8) | ((*dp >> 8) & 0377), cnt);
            dp++;
            if (s <= 8)
               cnt--;
        }
        uptr->STATUS &= ~DTE_IND;
        dn_in_ptr = (dn_in_ptr + 1) & 0x1f;
    } else {
        /* Transfer from 10 */
        in->dptr = 0;
        in->dcnt = 0;
        /* Read in count */
        if (!Mem_read_byte(1, &data1, 0))
            goto error;
        in->cnt = data1;
        cnt = in->cnt-2;
        if (!Mem_read_byte(1, &data1, 0))
            goto error;
        in->func = data1;
        cnt -= 2;
        if (!Mem_read_byte(1, &data1, 0))
            goto error;
        in->dev = data1;
        cnt -= 2;
        if (!Mem_read_byte(1, &data1, 0))
            goto error;
        in->spare = data1;
        cnt -= 2;
        sim_debug(DEBUG_DATA, &dn_dev, "DTE: Read CMD: %o c=%o f=%o %s s=%o d=%o\n",
                  dn_in_ptr, in->cnt, in->func,
                  ((in->func & 0377) > PRI_EMLDV)?"***":
                       pri_name[in->func & 0377], in->spare, in->dev);
        dp = &in->data[0];
        for (; cnt > 0; cnt -=2) {
            /* Read in data */
            if (!Mem_read_byte(1, dp, 0))
               goto error;
            sim_debug(DEBUG_DATA, &dn_dev, "DTE: Read data: %06o %03o %03o\n",
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
            if (Mem_deposit_word(1, dn_dt10_off + PRI_CMTW_STS, &word))
                goto error;
        } else {
            dn_in_ptr = (dn_in_ptr + 1) & 0x1f;
        }
    }
    word &= ~PRI_CMT_TOT;
    if (Mem_deposit_word(1, dn_dt10_off + PRI_CMTW_STS, &word))
        goto error;
    uptr->STATUS |= DTE_11DN;
    set_interrupt(DTE_DEVNUM, uptr->STATUS);
    dn_function(uptr);
}

/* Process primary protocol packets */
void
dn_function(UNIT *uptr)
{
    uint16    data1[32];
    int32     ch;
    struct _dn_queue *cmd;
    int       func;
    int       dev;

    /* Check if queue is empty */
    while (dn_in_cmd != dn_in_ptr) {
        if (((dn_out_res + 1) & 0x1f) == dn_out_ptr) {
            sim_debug(DEBUG_DATA, &dn_dev, "DTE: func out full %d %d\n",
                          dn_out_res, dn_out_ptr);
            return;
        }
        cmd = &dn_in[dn_in_cmd];
        dev = cmd->dev & 0377;
        func = cmd->func & 0377;
        sim_debug(DEBUG_DATA, &dn_dev,
                "DTE: func %o %02o %s dev %o cnt %d dcnt %d\n",
                dn_in_cmd, func, (func > PRI_EMLDV) ? "***" :  pri_name[func],
                cmd->dev, cmd->dcnt, cmd->dptr );
        switch (func) {
        case PRI_EM2EI:            /* Initial message to 11 */
               break;

        case PRI_EM2TI:            /* Replay to initial message. */
               data1[0] = (6 << 8) | 5;
               data1[1] = (0 << 8) | 0xc0;
               data1[2] = (1 << 8) | 0;
               if (dn_queue(01, PRI_DN60, 3, data1) == 0)
                   return;
                break;
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
                   if (dn_queue(PRI_EMHDR | PRI_IND_FLG, PRI_EMCLK, 6, data1) == 0)
                       return;
               }
               break;

        case PRI_EMSTR:            /* String data */


               /* Handle terminal data */
               if (dev == PRI_EMDLS) {
                   int   ln = cmd->sdev;
                   struct _buffer *otty;
                   if (ln == PRI_CTYDV)
                       goto cty;
                   break;
               }

               if (dev == PRI_EMCTY) {
cty:
#if 0
                   sim_activate(&dn_unit[1], 100);
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
                            sim_debug(DEBUG_DATA, &dn_dev,"DN queue %o\n",ch);
                        }
                        cmd->dptr++;
                   }
#endif
                   if (cmd->dptr != cmd->dcnt)
                       return;
               }
               break;

        case PRI_EMSNA:            /* Send all (ttys) */
               break;

        case PRI_EMLNC:            /* Line-Char */
               break;

        case PRI_EMOPS:
               break;

        case PRI_EMRDS:            /* Request device status */
               break;

        case PRI_EMHDS:            /* Here is device status */
               break;
        case PRI_EMLDV:            /* Load LP VFU */
               break;

        case PRI_EMLDR:            /* Load LP RAM */
               break;


        case PRI_EMFLO:            /* Flush output */
               break;

        case PRI_EMDSC:            /* Dataset connect */
               break;

        case PRI_EMHUD:            /* Hang up dataset */

        case PRI_EMXOF:            /* XOFF line */
               break;

        case PRI_EMXON:            /* XON line */
               break;

        case PRI_EMHLS:            /* Here is line speeds */
               break;

        case PRI_EMHLA:            /* Here is line allocation */
        case PRI_EMRBI:            /* Reboot information */
        case PRI_EMAKA:            /* Ack ALL */
        case PRI_EMTDO:            /* Turn device On/Off */
               break;

        case PRI_EMEDR:            /* Enable/Disable line */
               break;
        default:
               break;
        }
        /* Mark command as finished */
        cmd->cnt = 0;
        dn_in_cmd = (dn_in_cmd + 1) & 0x1F;
    }
}

/*
 * Handle primary protocol,
 * Send to 10 when requested.
 */
void dn_transfer(UNIT *uptr) {
    uint16   cnt;
    uint16   scnt;
    struct   _dn_queue *out;
    uint16   *dp;
int i;


    /* Check if Queue empty */
    if (dn_out_res == dn_out_ptr)
        return;

    out = &dn_out[dn_out_ptr];
    uptr->STATUS &= ~DTE_TO11;
    clr_interrupt(DTE_DEVNUM);
for (i = 0; i < 8; i++)
   sim_debug(DEBUG_DETAIL, &dn_dev, "EB Sword %o %012llo\n", i, M[eb_ptr + 0150+i]);
for (i = 0; i <200; i++) {
uint64 word;
   if (Mem_examine_word(1, i, &word))
      break;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 Sword %3o %012llo\n", i, word);
}

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
           sim_debug(DEBUG_DATA, &dn_dev, "DTE: Send Idata: %06o %03o %03o\n",
                          *dp, *dp >> 8, *dp & 0377);
           if (Mem_write_byte(1, dp) == 0)
              goto error;
           dp++;
       }
       uptr->STATUS &= ~DTE_SIND;
    } else {
        sim_debug(DEBUG_DATA, &dn_dev, "DTE: %d %d send CMD: [%o] %o %o %o\n",
                dn_out_ptr, dn_out_res, scnt, out->cnt, out->func, out->dev);
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
       if (!Mem_write_byte(1, &cnt))
          goto error;
       if (!Mem_write_byte(1, &out->func))
          goto error;
       cnt -= 2;
       if (!Mem_write_byte(1, &out->dev))
          goto error;
       cnt -= 2;
       if (!Mem_write_byte(1, &out->spare))
           goto error;
       cnt -= 2;
       if (out->func & PRI_IND_FLG) {
           uint16 dwrd = out->dcnt;
           sim_debug(DEBUG_DATA, &dn_dev, "DTE: Indirect %o %o\n", cnt,
                              out->dcnt);
           dwrd |= (out->sdev << 8);
           if (!Mem_write_byte(1, &dwrd))
              goto error;
           uptr->STATUS |= DTE_SIND;
           goto done;
       }
       cnt -= 2;
       dp = &out->data[0];
       for (; cnt > 0; cnt -= 2) {
           sim_debug(DEBUG_DATA, &dn_dev, "DTE: Send data: %06o %03o %03o\n",
                          *dp, *dp >> 8, *dp & 0377);
           if (!Mem_write_byte(1, dp))
              goto error;
           dp++;
       }
    }
    out->cnt = 0;
    dn_out_ptr = (dn_out_ptr + 1) & 0x1f;
done:
    uptr->STATUS |= DTE_10DN;
    set_interrupt(DTE_DEVNUM, uptr->STATUS);
error:
    return;
}

/* Process input from CTY and TTY's to 10.  */
void
dn_input()
{
    uint16  data1;
    uint16  dataq[32];
    int     n;
    int     ln;
    int     save_ptr;
    char    ch;
    UNIT    *uptr = &dn_unit[0];

#if 0
    if ((uptr->STATUS & DTE_SEC) == 0) {
       /* Check if CTY done with input */
       if (cty_done) {
           data1 = PRI_CTYDV;
           if (dn_queue(PRI_EMLBE, PRI_EMDLS, 1, &data1) == 0)
               return;
           cty_done--;
       }
       /* Grab a chunck of input from CTY if any */
       n = 0;
       save_ptr = cty_in.out_ptr;
       while (not_empty(&cty_in) && n < 32) {
           ch = cty_in.buff[cty_in.out_ptr];
           inco(&cty_in);
           sim_debug(DEBUG_DETAIL, &dn_dev, "DN recieve %02x\n", ch);
           dataq[n++] = (PRI_CTYDV << 8) | ch;
       }
       if (n > 0 && dn_queue(PRI_EMLNC, PRI_EMDLS, n, dataq) == 0) {
           /* Restore the input pointer */
           cty_in.out_ptr = save_ptr;
           return;
       }
    }
#endif
}

/*
 * Queue up a packet to send to 10.
 */
int
dn_queue(int func, int dev, int dcnt, uint16 *data)
{
    uint16   *dp;
    struct   _dn_queue *out;

    /* Check if room in queue for this packet. */
    if (((dn_out_res + 1) & 0x1f) == dn_out_ptr) {
        sim_debug(DEBUG_DATA, &dn_dev, "DTE: %d %d out full\n", dn_out_res, dn_out_ptr);
        return 0;
    }
    out = &dn_out[dn_out_res];
    out->cnt = 10;
    out->func = func;
    out->dev = dev;
    out->dcnt = (dcnt-1)*2;
    out->spare = 0;
    sim_debug(DEBUG_DATA, &dn_dev, "DTE: %d %d queue resp: %o (%o) f=%o %s d=%o\n",
                    dn_out_ptr, dn_out_res, out->cnt, out->dcnt, out->func,
                    (out->func > PRI_EMLDV)? "***":pri_name[out->func], out->dev);
    for (dp = &out->data[0]; dcnt > 0; dcnt--) {
         *dp++ = *data++;
    }
   /* Advance pointer to next function */
   dn_out_res = (dn_out_res + 1) & 0x1f;
   return 1;
}


/*
 * If anything in queue, start a transfer, if one is not already
 * pending.
 */
int
dn_start(UNIT *uptr)
{
    uint64   word;
    int      dcnt;
int i;

    /* Check if queue empty */
    if (dn_out_ptr == dn_out_res)
        return 1;

    /* If there is interrupt pending, just return */
    if ((uptr->STATUS & (DTE_IND|DTE_10DB|DTE_11DB)) != 0)
        return 1;
    if (Mem_examine_word(1, dn_et11_off + PRI_CMTW_STS, &word)) {
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
    if (Mem_deposit_word(1, dn_dt10_off + PRI_CMTW_STS, &word))
        goto error;
    dcnt = dn_out[dn_out_ptr].cnt;
    if ((dn_out[dn_out_ptr].func & PRI_IND_FLG) == 0)
        dcnt += dn_out[dn_out_ptr].dcnt;
    /* Tell 10 something is ready */
    if ((uptr->STATUS & DTE_SIND) != 0) {
        dcnt = dn_out[dn_out_ptr].dcnt;
    }
    sim_debug(DEBUG_DATA, &dn_dev, "DTE: start: %012llo %o\n", word, dcnt);
    word = (uint64)dcnt;
    word |= (word << 18);
    if (Mem_deposit_word(1, dn_dt10_off + PRI_CMTW_CNT, &word))
        goto error;
for (i = 0; i < 8; i++)
   sim_debug(DEBUG_DETAIL, &dn_dev, "EB word %o %012llo\n", i, M[eb_ptr + 0150+i]);
for (i = 0; i <200; i++) {
   if (Mem_examine_word(1, i, &word))
      break;
    sim_debug(DEBUG_DETAIL, &dn_dev, "DN1 word %3o %012llo\n", i, word);
}
    uptr->STATUS |= DTE_10DB;
    set_interrupt(DTE_DEVNUM, uptr->STATUS);
    return 1;
}


/* Check for input from CTY and put on queue. */
t_stat dni_svc (UNIT *uptr)
{
    int32    ch;
    uint32   base = 0;
    UNIT     *optr = &dn_unit[0];

#if KI_22BIT
    base = eb_ptr;
#endif
    sim_clock_coschedule (uptr, tmxr_poll);
    dn_input();
    if ((optr->STATUS & (DTE_SEC)) == 0) {
        dn_function(uptr);  /* Process queue */
        dn_start(optr);
    }


#if 0
    /* If we have room see if any new lines */
    while (!full(&cty_in)) {
        ch = sim_poll_kbd ();
        if (ch & SCPE_KFLAG) {
            ch = 0177 & sim_tt_inpcvt(ch, TT_GET_MODE (uptr->flags));
            cty_in.buff[cty_in.in_ptr] =ch & 0377;
            inci(&cty_in);
            sim_debug(DEBUG_DETAIL, &dn_dev, "CTY char %o '%c'\n", ch,
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
        if (dn_dev.flags & TYPE_RSX20) {
            uptr->STATUS |= DTE_10DB;
            set_interrupt(DTE_DEVNUM, dn_unit[0].STATUS);
        }
    }
#endif
    return SCPE_OK;
}

/* Handle output of characters to CTY. Started whenever there is output pending */
t_stat dno_svc (UNIT *uptr)
{
#if 0
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
        sim_debug(DEBUG_DETAIL, &dn_dev, "DN outch %o '%c'\n", ch,
                            ((ch > 040 && ch < 0177)? ch: '.'));
    }
    cty_done++;
#endif
    return SCPE_OK;
}


/* Handle FE timer interrupts. And keepalive counts */
t_stat
dnrtc_srv(UNIT * uptr)
{
    UNIT     *optr = &dn_unit[0];

    sim_activate_after(uptr, 1000000/60);

    /* Update out keep alive timer if in secondary protocol */
    if ((optr->STATUS & DTE_SEC) == 0) {
        int      addr = 0154 + eb_ptr;
        uint64   word;

        (void)Mem_examine_word(1, dn_et11_off + PRI_CMTW_STS, &word);
        addr = (M[addr+1] + dn_off + PRI_CMTW_KAC) & RMASK;
        word = M[addr];
        word = (word + 1) & FMASK;
        M[addr] = word;
      sim_debug(DEBUG_EXP, &dn_dev, "DN keepalive %06o %012llo %06o\n",
                          addr, word, optr->STATUS);
    }

    return SCPE_OK;
}


t_stat dn_reset (DEVICE *dptr)
{
    dn_unit[0].STATUS = 0; //DTE_SEC;
    dn_unit[1].STATUS = 0;
    dn_unit[2].STATUS = 0;
    dn_proc_num = 0;
    dn_base = dn_proc_num + 1;
    dn_off = 0;
    dn_dt10_off = 16;
    dn_et10_off = 050;
    dn_et11_off = 033;
    sim_activate(&dn_unit[2], 1000);
    return SCPE_OK;
}


t_stat
dn_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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
dn_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
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


t_stat dn_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprint_reg_help (st, &dn_dev);
return SCPE_OK;
}

const char *dn_description (DEVICE *dptr)
{
    return "DN Network interface";
}


#endif
#endif
