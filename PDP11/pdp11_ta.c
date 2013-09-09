/* pdp11_ta.c: PDP-11 cassette tape simulator

   Copyright (c) 2007-2013, Robert M Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ATAION OF CONTRATA, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNETAION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   ta           TA11/TU60 cassette tape
   
   06-Jun-13    RMS     Reset must set RDY (Ian Hammond)
                        Added CAPS-11 bootstrap (Ian Hammond)
   06-Aug-07    RMS     Foward op at BOT skips initial file gap

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.

   Cassette format differs in one very significant way: it has file gaps
   rather than file marks.  If the controller spaces or reads into a file
   gap and then reverses direction, the file gap is not seen again.  This
   is in contrast to magnetic tapes, where the file mark is a character
   sequence and is seen again if direction is reversed.  In addition,
   cassettes have an initial file gap which is automatically skipped on
   forward operations from beginning of tape.
*/

#include "pdp11_defs.h"
#include "sim_tape.h"

#define TA_NUMDR        2                               /* #drives */
#define FNC             u3                              /* unit function */
#define UST             u4                              /* unit status */
#define TA_SIZE         93000                           /* chars/tape */
#define TA_MAXFR        (TA_SIZE)                       /* max record lnt */

/* Control/status - TACS */

#define TACS_ERR        (1 << CSR_V_ERR)                /* error */
#define TACS_CRC        0040000                         /* CRC */
#define TACS_BEOT       0020000                         /* BOT/EOT */
#define TACS_WLK        0010000                         /* write lock */
#define TACS_EOF        0004000                         /* end file */
#define TACS_TIM        0002000                         /* timing */
#define TACS_EMP        0001000                         /* empty */
#define TACS_V_UNIT     8                               /* unit */
#define TACS_M_UNIT     (TA_NUMDR - 1)
#define TACS_UNIT       (TACS_M_UNIT << TACS_V_UNIT)
#define TACS_TR         (1 << CSR_V_DONE)               /* transfer req */
#define TACS_IE         (1 << CSR_V_IE)                 /* interrupt enable */
#define TACS_RDY        0000040                         /* ready */
#define TACS_ILBS       0000020                         /* start CRC */
#define TACS_V_FNC      1                               /* function */
#define TACS_M_FNC      07
#define  TACS_WFG        00
#define  TACS_WRITE      01
#define  TACS_READ       02
#define  TACS_SRF        03
#define  TACS_SRB        04
#define  TACS_SFF        05
#define  TACS_SFB        06
#define  TACS_REW        07
#define TACS_2ND        010
#define TACS_3RD        030
#define TACS_FNC        (TACS_M_FNC << TACS_V_FNC)
#define TACS_GO         (1 << CSR_V_GO)                 /* go */
#define TACS_W          (TACS_UNIT|TACS_IE|TACS_ILBS|TACS_FNC)
#define TACS_XFRERR     (TACS_ERR|TACS_CRC|TACS_WLK|TACS_EOF|TACS_TIM)
#define GET_UNIT(x)     (((x) >> TACS_V_UNIT) & TACS_M_UNIT)
#define GET_FNC(x)      (((x) >> TACS_V_FNC) & TACS_M_FNC)

/* Function code flags */

#define OP_WRI          01                              /* op is a write */
#define OP_REV          02                              /* op is rev motion */
#define OP_FWD          04                              /* op is fwd motion */

/* Unit status flags */

#define UST_REV         (OP_REV)                        /* last op was rev */
#define UST_GAP         01                              /* last op hit gap */

extern int32 int_req[IPL_HLVL];

uint32 ta_cs = 0;                                       /* control/status */
uint32 ta_idb = 0;                                      /* input data buf */
uint32 ta_odb = 0;                                      /* output data buf */
uint32 ta_write = 0;                                    /* TU60 write flag */
uint32 ta_bptr = 0;                                     /* buf ptr */
uint32 ta_blnt = 0;                                     /* buf length */
int32 ta_stime = 1000;                                  /* start time */
int32 ta_ctime = 100;                                   /* char latency */
uint32 ta_stopioe = 1;                                  /* stop on error */
uint8 *ta_xb = NULL;                                    /* transfer buffer */
static uint8 ta_fnc_tab[TACS_M_FNC + 1] = {
    OP_WRI|OP_FWD, OP_WRI|OP_FWD, OP_FWD, OP_REV,
    OP_REV       , OP_FWD,        OP_FWD, 0
    };

DEVICE ta_dev;
t_stat ta_rd (int32 *data, int32 PA, int32 access);
t_stat ta_wr (int32 data, int32 PA, int32 access);
t_stat ta_svc (UNIT *uptr);
t_stat ta_reset (DEVICE *dptr);
t_stat ta_attach (UNIT *uptr, char *cptr);
t_stat ta_detach (UNIT *uptr);
t_stat ta_boot (int32 unitno, DEVICE *dptr);
void ta_go (void);
t_stat ta_map_err (UNIT *uptr, t_stat st);
UNIT *ta_busy (void);
void ta_set_tr (void);
uint32 ta_updsta (UNIT *uptr);
uint32 ta_crc (uint8 *buf, uint32 cnt);
t_stat ta_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *ta_description (DEVICE *dptr);

/* TA data structures

   ta_dev       TA device descriptor
   ta_unit      TA unit list
   ta_reg       TA register list
   ta_mod       TA modifier list
*/
#define IOLN_TA         004

DIB ta_dib = {
    IOBA_AUTO, IOLN_TA, &ta_rd, &ta_wr,
    1, IVCL (TA), VEC_AUTO, { NULL }, IOLN_TA
    };

UNIT ta_unit[] = {
    { UDATA (&ta_svc, UNIT_ATTABLE+UNIT_ROABLE, TA_SIZE) },
    { UDATA (&ta_svc, UNIT_ATTABLE+UNIT_ROABLE, TA_SIZE) },
    };

REG ta_reg[] = {
    { ORDATAD (TACS, ta_cs, 16, "control/status register") },
    { ORDATAD (TAIDB, ta_idb, 8, "input data buffer") },
    { ORDATAD (TAODB, ta_odb, 8, "output data buffer") },
    { FLDATAD (WRITE, ta_write, 0, "TA60 write operation flag") },
    { FLDATAD (INT, IREQ (TA), INT_V_TA, "interrupt request") },
    { FLDATAD (ERR, ta_cs, CSR_V_ERR, "error flag") },
    { FLDATAD (TR, ta_cs, CSR_V_DONE, "transfer request flag") },
    { FLDATAD (IE, ta_cs, CSR_V_IE, "interrupt enable flag") },
    { DRDATAD (BPTR, ta_bptr, 17, "buffer pointer") },
    { DRDATAD (BLNT, ta_blnt, 17, "buffer length") },
    { DRDATAD (STIME, ta_stime, 24, "operation start time"), PV_LEFT + REG_NZ },
    { DRDATAD (CTIME, ta_ctime, 24, "character latency"), PV_LEFT + REG_NZ },
    { FLDATAD (STOP_IOE, ta_stopioe, 0, "stop on I/O errors flag") },
    { URDATA (UFNC, ta_unit[0].FNC, 8, 5, 0, TA_NUMDR, REG_HRO), },
    { URDATA (UST, ta_unit[0].UST, 8, 2, 0, TA_NUMDR, REG_HRO), },
    { URDATAD (POS, ta_unit[0].pos, 10, T_ADDR_W, 0,
              TA_NUMDR, PV_LEFT | REG_RO, "position") },
    { ORDATA (DEVADDR, ta_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, ta_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB ta_mod[] = {
    { MTUF_WLK,        0, "write enabled", "WRITEENABLED", 
        NULL, NULL, NULL, "Write enable tape drive" },
    { MTUF_WLK, MTUF_WLK, "write locked",  "LOCKED", 
        NULL, NULL, NULL, "Write lock tape drive"  },
//    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
//      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { MTAB_XTD|MTAB_VUN, 0, "CAPACITY", NULL,
      NULL, &sim_tape_show_capac, NULL, "Display tape capacity" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, "VECTOR", "VECTOR",
        &set_vec, &show_vec, NULL, "Interrupt vector" },
    { 0 }
    };

DEVICE ta_dev = {
    "TA", ta_unit, ta_reg, ta_mod,
    TA_NUMDR, 10, 31, 1, 8, 8,
    NULL, NULL, &ta_reset,
    &ta_boot, &ta_attach, &ta_detach,
    &ta_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_UBUS | DEV_TAPE, 0,
    NULL, NULL, NULL, &ta_help, NULL, NULL,
    &ta_description 
    };

/* I/O dispatch routines, I/O addresses 17777500 - 17777503

   17777500     TACS    read/write
   17777502     TADB    read/write
*/

t_stat ta_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* TACSR */
        *data = ta_updsta (NULL);                       /* update status */
        break;

    case 1:                                             /* TADB */
        *data = ta_idb;                                 /* return byte */
        ta_cs &= ~TACS_TR;                              /* clear tra req */
        ta_updsta (NULL);
        break;
    }

return SCPE_OK;
}

t_stat ta_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* TACS */
        if (access == WRITEB) data = (PA & 1)?          /* byte write? */
            (ta_cs & 0377) | (data << 8):               /* merge old */
            (ta_cs & ~0377) | data;
        ta_cs = (ta_cs & ~TACS_W) | (data & TACS_W);    /* merge new */
        if ((data & CSR_GO) && !ta_busy ())             /* go, not busy? */
            ta_go ();                                   /* start operation */
        if (ta_cs & TACS_ILBS)                          /* ILBS inhibits TR */
            ta_cs &= ~TACS_TR;
        break;

    case 1:                                             /* TADB */
        if (PA & 1)                                     /* ignore odd byte */
            break;
        ta_odb = data;                                  /* return byte */
        ta_cs &= ~TACS_TR;                              /* clear tra req */
        break;
        }                                               /* end switch */

ta_updsta (NULL);                                       /* update status */
return SCPE_OK;
}

/* Start a new operation - cassette is not busy */

void ta_go (void)
{
UNIT *uptr = ta_dev.units + GET_UNIT (ta_cs);
uint32 fnc = GET_FNC (ta_cs);
uint32 flg = ta_fnc_tab[fnc];
uint32 old_ust = uptr->UST;

if (DEBUG_PRS (ta_dev)) fprintf (sim_deb,
    ">>TA start: op=%o, old_sta = %o, pos=%d\n",
    fnc, uptr->UST, uptr->pos);
ta_cs &= ~(TACS_XFRERR|TACS_EMP|TACS_TR|TACS_RDY);      /* clr err, tr, rdy */
ta_bptr = 0;                                            /* init buffer */
ta_blnt = 0;
if ((uptr->flags & UNIT_ATT) == 0) {
    ta_cs |= TACS_ERR|TACS_EMP|TACS_RDY;
    return;
    }
if (flg & OP_WRI) {                                     /* write op? */
    if (sim_tape_wrp (uptr)) {                          /* locked? */
        ta_cs |= TACS_ERR|TACS_WLK|TACS_RDY;            /* don't start */
        return;
        }
    ta_odb = 0;
    ta_write = 1;
    }
else {
    ta_idb = 0;
    ta_write = 0;
    }
ta_cs &= ~TACS_BEOT;                                    /* tape in motion */
uptr->FNC = fnc;                                        /* save function */
if ((fnc != TACS_REW) && !(flg & OP_WRI)) {             /* spc/read cmd? */
    t_mtrlnt t;
    t_stat st;
    uptr->UST = flg & UST_REV;                          /* save direction */
    if (sim_tape_bot (uptr) && (flg & OP_FWD)) {        /* spc/read fwd bot? */
        st = sim_tape_rdrecf (uptr, ta_xb, &t, TA_MAXFR); /* skip file gap */
        if (st != MTSE_TMK)                             /* not there? */
            sim_tape_rewind (uptr);                     /* restore tap pos */
        else old_ust = 0;                               /* defang next */
        }
    if ((old_ust ^ uptr->UST) == (UST_REV|UST_GAP)) {   /* reverse in gap? */
        if (uptr->UST)                                  /* skip file gap */
            sim_tape_rdrecr (uptr, ta_xb, &t, TA_MAXFR);
        else sim_tape_rdrecf (uptr, ta_xb, &t, TA_MAXFR);
        if (DEBUG_PRS (ta_dev))
            fprintf (sim_deb, ">>TA skip gap: op=%o, old_sta = %o, pos=%d\n",
                     fnc, uptr->UST, uptr->pos);
        }
    }
else uptr->UST = 0;
sim_activate (uptr, ta_stime);                          /* schedule op */
return;
}

/* Unit service */

t_stat ta_svc (UNIT *uptr)
{
uint32 i, crc;
uint32 flg = ta_fnc_tab[uptr->FNC & TACS_M_FNC];
t_mtrlnt tbc;
t_stat st, r;

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    ta_cs |= TACS_ERR|TACS_EMP|TACS_RDY;
    ta_updsta (uptr);                                   /* update status */
    return (ta_stopioe? SCPE_UNATT: SCPE_OK);
    }
if (((flg & OP_FWD) && sim_tape_eot (uptr)) ||          /* illegal motion? */
    ((flg & OP_REV) && sim_tape_bot (uptr))) {
    ta_cs |= TACS_ERR|TACS_BEOT|TACS_RDY;               /* error */
    ta_updsta (uptr);
    return SCPE_OK;
    }

r = SCPE_OK;
switch (uptr->FNC) {                                    /* case on function */

    case TACS_READ:                                     /* read start */
        st = sim_tape_rdrecf (uptr, ta_xb, &ta_blnt, TA_MAXFR); /* get rec */
        if (st == MTSE_RECE)                            /* rec in err? */
            ta_cs |= TACS_ERR|TACS_CRC;
        else if (st != MTSE_OK) {                       /* other error? */
            r = ta_map_err (uptr, st);                  /* map error */
            break;
            }
        crc = ta_crc (ta_xb, ta_blnt);                  /* calculate CRC */
        ta_xb[ta_blnt++] = (crc >> 8) & 0377;           /* append to buffer */
        ta_xb[ta_blnt++] = crc & 0377;
        uptr->FNC |= TACS_2ND;                          /* next state */
        sim_activate (uptr, ta_ctime);                  /* sched next char */
        return SCPE_OK;

    case TACS_READ|TACS_2ND:                            /* read char */
        if (ta_bptr < ta_blnt)                          /* more chars? */
            ta_idb = ta_xb[ta_bptr++];
        else {                                          /* no */
            ta_idb = 0;
            ta_cs |= TACS_ERR|TACS_CRC;                 /* overrun */
            break;                                      /* tape stops */
            }
        if (ta_cs & TACS_ILBS) {                        /* CRC seq? */
            uptr->FNC |= TACS_3RD;                      /* next state */
            sim_activate (uptr, ta_stime);              /* sched CRC chk */
            }
        else {
            ta_set_tr ();                               /* set tra req */
            sim_activate (uptr, ta_ctime);              /* sched next char */
            }
        return SCPE_OK;

    case TACS_READ|TACS_3RD:                            /* second read CRC */
        if (ta_bptr != ta_blnt) {                       /* partial read? */
            crc = ta_crc (ta_xb, ta_bptr + 2);          /* actual CRC */
            if (crc != 0)                               /* must be zero */
                ta_cs |= TACS_ERR|TACS_CRC;
            }
         break;                                         /* read done */

    case TACS_WRITE:                                    /* write start */
        for (i = 0; i < TA_MAXFR; i++)                  /* clear buffer */
            ta_xb[i] = 0;
        ta_set_tr ();                                   /* set tra req */
        uptr->FNC |= TACS_2ND;                          /* next state */
        sim_activate (uptr, ta_ctime);                  /* sched next char */
        return SCPE_OK;

    case TACS_WRITE|TACS_2ND:                           /* write char */
        if (ta_cs & TACS_ILBS) {                        /* CRC seq? */
            uptr->FNC |= TACS_3RD;                      /* next state */
            sim_activate (uptr, ta_stime);              /* sched wri done */
            }
        else {
            if ((ta_bptr < TA_MAXFR) &&                 /* room in buf? */
                ((uptr->pos + ta_bptr) < uptr->capac))  /* room on tape? */
                ta_xb[ta_bptr++] = ta_odb;              /* store char */
            ta_set_tr ();                               /* set tra req */
            sim_activate (uptr, ta_ctime);              /* sched next char */
            }
        return SCPE_OK;

    case TACS_WRITE|TACS_3RD:                           /* write CRC */
        if (ta_bptr) {                                  /* anything to write? */
           if ((st = sim_tape_wrrecf (uptr, ta_xb, ta_bptr)))/* write, err? */
               r = ta_map_err (uptr, st);               /* map error */
           }
        break;                                          /* op done */

    case TACS_WFG:                                      /* write file gap */
        if ((st = sim_tape_wrtmk (uptr)))               /* write tmk, err? */
            r = ta_map_err (uptr, st);                  /* map error */
        break;

    case TACS_REW:                                      /* rewind */
        sim_tape_rewind (uptr);
        ta_cs |= TACS_BEOT;                             /* bot, no error */
        break;

    case TACS_SRB:                                      /* space rev blk */
        if ((st = sim_tape_sprecr (uptr, &tbc)))        /* space rev, err? */
            r = ta_map_err (uptr, st);                  /* map error */
         break;

    case TACS_SRF:                                      /* space rev file */
        while ((st = sim_tape_sprecr (uptr, &tbc)) == MTSE_OK) ;
        if (st == MTSE_TMK)                             /* if tape mark, */
            ta_cs |= TACS_EOF;                          /* set EOF, no err */
        else r = ta_map_err (uptr, st);                 /* else map error */
        break;

    case TACS_SFB:                                      /* space fwd blk */
        if ((st = sim_tape_sprecf (uptr, &tbc)))        /* space rev, err? */
            r = ta_map_err (uptr, st);                  /* map error */
        ta_cs |= TACS_CRC;                              /* CRC sets, no err */
        break;

    case TACS_SFF:                                      /* space fwd file */
        while ((st = sim_tape_sprecf (uptr, &tbc)) == MTSE_OK) ;
        if (st == MTSE_TMK)                             /* if tape mark, */
            ta_cs |= TACS_EOF;                          /* set EOF, no err */
        else r = ta_map_err (uptr, st);                 /* else map error */
        break;

    default:                                            /* never get here! */
        return SCPE_IERR;        
        }                                               /* end case */

ta_cs |= TACS_RDY;                                      /* set ready */
ta_updsta (uptr);                                       /* update status */
if (DEBUG_PRS (ta_dev))
    fprintf (sim_deb, ">>TA done: op=%o, status = %o, dstatus = %o, pos=%d\n",
             uptr->FNC, ta_cs, uptr->UST, uptr->pos);
return r;
}

/* Update controller status */

uint32 ta_updsta (UNIT *uptr)
{
if (uptr == NULL) {                                     /* unit specified? */
    if ((uptr = ta_busy ()) == NULL)                    /* use busy */
        uptr = ta_dev.units + GET_UNIT (ta_cs);         /* use sel unit */
    }
else if (ta_cs & TACS_EOF)                              /* save EOF */
    uptr->UST |= UST_GAP;
if (uptr->flags & UNIT_ATT)                             /* attached? */
    ta_cs &= ~TACS_EMP;
else ta_cs |= TACS_EMP|TACS_RDY;                        /* no, empty, ready */
if ((ta_cs & TACS_IE) &&                                /* int enabled? */
    (ta_cs & (TACS_TR|TACS_RDY)))                       /* req or ready? */
    SET_INT (TA);                                       /* set int req */
else CLR_INT (TA);                                      /* no, clr int req */
return ta_cs;
}

/* Set transfer request */

void ta_set_tr (void)
{
if (ta_cs & TACS_TR)                                    /* flag still set? */
    ta_cs |= (TACS_ERR|TACS_TIM);
else ta_cs |= TACS_TR;                                  /* set xfr req */
if (ta_cs & TACS_IE)                                    /* if ie, int req */
    SET_INT (TA);
return;
}

/* Test if controller busy */

UNIT *ta_busy (void)
{
uint32 u;
UNIT *uptr;

for (u = 0; u < TA_NUMDR; u++) {                        /* loop thru units */
    uptr = ta_dev.units + u;
    if (sim_is_active (uptr))
        return uptr;
    }
return NULL;
}

/* Calculate CRC on buffer */

uint32 ta_crc (uint8 *buf, uint32 cnt)
{
uint32 crc, i, j;

crc = 0;
for (i = 0; i < cnt; i++) {
    crc = crc ^ (((uint32) buf[i]) << 8);
    for (j = 0; j < 8; j++) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xA001;
        else crc = crc >> 1;
        }
    }
return crc;
}

/* Map error status */

t_stat ta_map_err (UNIT *uptr, t_stat st)
{
switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* unattached */
        ta_cs |= TACS_ERR|TACS_CRC;
    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;                               /* never get here! */

    case MTSE_TMK:                                      /* end of file */
        ta_cs |= TACS_ERR|TACS_EOF;
        break;

    case MTSE_IOERR:                                    /* IO error */
        ta_cs |= TACS_ERR|TACS_CRC;                     /* set crc err */
        if (ta_stopioe)
            return SCPE_IOERR;
        break;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        ta_cs |= TACS_ERR|TACS_CRC;                     /* set crc err */
        return SCPE_MTRLNT;

    case MTSE_RECE:                                     /* record in error */
    case MTSE_EOM:                                      /* end of medium */
        ta_cs |= TACS_ERR|TACS_CRC;                     /* set crc err */
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        ta_cs |= TACS_ERR|TACS_BEOT;                    /* set bot */
        break;

    case MTSE_WRP:                                      /* write protect */
        ta_cs |= TACS_ERR|TACS_WLK;                     /* set wlk err */
        break;
        }

return SCPE_OK;
}

/* Reset routine */

t_stat ta_reset (DEVICE *dptr)
{
uint32 u;
UNIT *uptr;

ta_cs = TACS_RDY;                                       /* init sets RDY */
ta_idb = 0;
ta_odb = 0;
ta_write = 0;
ta_bptr = 0;
ta_blnt = 0;
CLR_INT (TA);                                           /* clear interrupt */
for (u = 0; u < TA_NUMDR; u++) {                        /* loop thru units */
    uptr = ta_dev.units + u;
    sim_cancel (uptr);                                  /* cancel activity */
    sim_tape_reset (uptr);                              /* reset tape */
    }
if (ta_xb == NULL)
    ta_xb = (uint8 *) calloc (TA_MAXFR + 2, sizeof (uint8));
if (ta_xb == NULL)
    return SCPE_MEM;
return auto_config (0, 0);
}

/* Attach routine */

t_stat ta_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);
if (r != SCPE_OK)
    return r;
ta_updsta (NULL);
uptr->UST = 0;
return r;
}

/* Detach routine */

t_stat ta_detach (UNIT* uptr)
{
t_stat r;

if (!(uptr->flags & UNIT_ATT))                          /* check attached */
    return SCPE_OK;
r = sim_tape_detach (uptr);
ta_updsta (NULL);
uptr->UST = 0;
return r;
}

/* Bootstrap routine */

#define BOOT_START      01000                           /* start */
#define BOOT_ENTRY      (BOOT_START)
#define BOOT_CSR        (BOOT_START + 002)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (uint16))

static const uint16 boot_rom[] = {
0012700,                /* mov #tacs,r0 */
0177500,
0005010,                /* clr (r0) */
0010701,                /* 3$: mov pc,r1 */
0062701,                /* add #20-here,r1 */
0000052,
0012702,                /* mov #375,r2 */
0000375,
0112103,                /* movb (r1)+,r3 */
0112110,                /* 5$: movb (r1)+,(r0) */
0100413,                /* bmi 15$ */
0130310,                /* 10$: bitb r3,(r0) */
0001776,                /* beq 10$ */
0105202,                /* incb r2 */
0100772,                /* bmi 5$ */
0116012,                /* movb 2(r0),r2 */
0000002,
0120337,                /* cmpb r3,@#0 */
0000000,
0001767,                /* beq 10$ */
0000000,                /* 12$: halt */
0000755,                /* br 3$ */
0005710,                /* 15$: tst (r0) */
0100774,                /* bmi 12$ */
0005007,                /* clr pc */
0017640,                /* $20: (data) */
0002415,
0112024
};

t_stat ta_boot (int32 unitno, DEVICE *dptr)
{
int32 i;
extern int32 saved_PC;
extern uint16 *M;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_CSR >> 1] = ta_dib.ba & DMASK;
saved_PC = BOOT_ENTRY;
return SCPE_OK;
}

t_stat ta_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
const char *const text =
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
" TA11/TA60 Cassette Tape (CT)\n"
"\n"
" The TA11 is a programmed I/O controller supporting two cassette drives\n"
" (0 and 1).  The TA11 can be used like a small magtape under RT11 and\n"
" RSX-11M, and with the CAPS-11 operating system.  Cassettes are simulated\n"
" as magnetic tapes with a fixed capacity (93,000 characters).  The tape\n"
" format is always SimH standard.\n"
" The TA11 is disabled by default.\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"\n";
fprintf (st, "%s", text);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error         processed as\n");
fprintf (st, "    not attached  tape not ready\n\n");
fprintf (st, "    end of file   end of medium\n");
fprintf (st, "    OS I/O error  fatal tape error\n\n");
sim_tape_attach_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

char *ta_description (DEVICE *dptr)
{
return "TA11/TA60 Cassette Tape";
}
